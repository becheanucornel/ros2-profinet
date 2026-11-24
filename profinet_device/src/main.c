/* SPDX-License-Identifier: GPL-3.0-only */
/* profinet_device: PROFINET IO device that exchanges its data with the ROS 2 node
 * over a Unix socket. Linux only. */
#define _GNU_SOURCE
#include "device.h"
#include "protocol.h"

#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define NODE_TIMEOUT_MS  500
#define OUTPUT_PERIOD_MS 10
#define CYCLIC_PRIORITY  15

static pn_state_t state;
static pthread_mutex_t mutex;

static void state_lock (void)
{
   pthread_mutex_lock (&mutex);
}

static void state_unlock (void)
{
   pthread_mutex_unlock (&mutex);
}

static int64_t now_ms (void)
{
   struct timespec t;

   clock_gettime (CLOCK_MONOTONIC, &t);
   return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/******************************** Node socket ********************************/

static int listen_on (const char * path)
{
   struct sockaddr_un addr = {.sun_family = AF_UNIX};
   int fd;

   if (strlen (path) >= sizeof (addr.sun_path))
   {
      errno = ENAMETOOLONG;
      return -1;
   }
   strcpy (addr.sun_path, path);
   fd = socket (AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
   if (fd < 0)
   {
      return -1;
   }
   (void)unlink (path);
   /* Owner and group only: whoever can write here controls the PLC's inputs. */
   if (
      bind (fd, (struct sockaddr *)&addr, sizeof (addr)) != 0 || chmod (path, 0660) != 0 ||
      listen (fd, 4) != 0)
   {
      close (fd);
      return -1;
   }
   return fd;
}

static void refuse (int fd, protocol_result_t reason)
{
   uint8_t packet[PROTOCOL_MAX_PACKET];
   struct pollfd p = {fd, POLLIN, 0};

   /* Closing with unread data would discard the answer. */
   (void)poll (&p, 1, 50);
   while (recv (fd, packet, sizeof (packet), 0) > 0)
   {
   }
   (void)send (fd, packet, protocol_reject (reason, packet), MSG_DONTWAIT | MSG_NOSIGNAL);
   close (fd);
}

static void * node_thread (void * arg)
{
   uint8_t packet[PROTOCOL_MAX_PACKET];
   int listener = *(int *)arg;
   int node = -1;
   int64_t last_input = 0;
   int64_t next_output = 0;

   for (;;)
   {
      struct pollfd fds[2] = {{listener, POLLIN, 0}, {node, POLLIN, 0}};
      bool lost = false;
      int64_t now;

      (void)poll (fds, 2, OUTPUT_PERIOD_MS);
      now = now_ms();

      if (fds[0].revents & POLLIN)
      {
         int fd = accept4 (listener, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);

         if (fd >= 0 && node >= 0)
         {
            refuse (fd, PROTOCOL_REJECT_BUSY);
         }
         else if (fd >= 0)
         {
            node = fd;
            last_input = now;
            next_output = now;
            continue;
         }
      }
      if (node < 0)
      {
         continue;
      }
      if (fds[1].revents != 0)
      {
         ssize_t n = recv (node, packet, sizeof (packet), 0);

         if (n > 0)
         {
            protocol_result_t result;

            state_lock();
            result = protocol_receive (&state, packet, (size_t)n);
            state_unlock();
            if (result == PROTOCOL_OK)
            {
               last_input = now;
            }
            else
            {
               printf ("node refused (reason %d)\n", (int)result);
               refuse (node, result);
               node = -1;
               lost = true;
            }
         }
         else if (n == 0 || (errno != EAGAIN && errno != EINTR))
         {
            lost = true;
         }
      }
      if (now - last_input > NODE_TIMEOUT_MS)
      {
         lost = true;
      }
      if (lost)
      {
         state_lock();
         pn_state_node_lost (&state);
         state_unlock();
         if (node >= 0)
         {
            close (node);
         }
         node = -1;
         printf ("node disconnected\n");
      }
      else if (now >= next_output)
      {
         size_t length = 0;

         next_output = now + OUTPUT_PERIOD_MS;
         state_lock();
         /* Before its HELLO, a new node would get data for the previous layout. */
         if (state.node_connected)
         {
            length = protocol_outputs (&state, packet);
         }
         state_unlock();
         if (length > 0)
         {
            /* A full socket only delays the data; a dead node is found above. */
            (void)send (node, packet, length, MSG_DONTWAIT | MSG_NOSIGNAL);
         }
      }
   }
   return NULL;
}

/*********************************** Main ************************************/

/* The MAC address without colons, which is unique enough. */
static void read_serial_number (const char * interface, char * serial, size_t size)
{
   char path[128];
   size_t n = 0;
   int c;
   FILE * f;

   snprintf (path, sizeof (path), "/sys/class/net/%s/address", interface);
   f = fopen (path, "r");
   while (f != NULL && n + 1 < size && (c = fgetc (f)) != EOF)
   {
      if (c != ':' && c != '\n')
      {
         serial[n++] = (char)c;
      }
   }
   serial[n] = '\0';
   if (f != NULL)
   {
      fclose (f);
   }
}

static void usage (void)
{
   printf (
      "profinet_device -V VENDOR_ID -D DEVICE_ID -i INTERFACE -n STATION_NAME"
      " [-s SOCKET] [-d STORAGE_DIRECTORY]\n");
}

/* An ID as it is written in the GSDML file, such as 0x1234. 0 is not an ID. */
static uint16_t parse_id (const char * text)
{
   char * end;
   unsigned long id;

   if (strncmp (text, "0x", 2) != 0 || strspn (text + 2, "0123456789abcdefABCDEF") != 4)
   {
      return 0;
   }
   id = strtoul (text, &end, 16);
   return *end == '\0' ? (uint16_t)id : 0;
}

int main (int argc, char * argv[])
{
   pn_device_config_t config = {
      .storage = "/var/lib/profinet_device",
      .lock = state_lock,
      .unlock = state_unlock,
      .state = &state};
   const char * socket_path = "/run/profinet_device/profinet_device.sock";
   const struct sched_param priority = {.sched_priority = CYCLIC_PRIORITY};
   pthread_mutexattr_t attr;
   char serial[17];
   struct timespec next;
   pthread_t thread;
   int listener;
   int option;

   while ((option = getopt (argc, argv, "V:D:i:n:s:d:h")) != -1)
   {
      switch (option)
      {
      case 'V':
         config.vendor_id = parse_id (optarg);
         break;
      case 'D':
         config.device_id = parse_id (optarg);
         break;
      case 'i':
         config.interface = optarg;
         break;
      case 'n':
         config.station_name = optarg;
         break;
      case 's':
         socket_path = optarg;
         break;
      case 'd':
         config.storage = optarg;
         break;
      default:
         usage();
         return 2;
      }
   }
   if (config.vendor_id == 0 || config.device_id == 0)
   {
      fprintf (stderr, "the vendor ID and device ID of your organisation are needed\n");
      usage();
      return 2;
   }
   if (config.interface == NULL || config.station_name == NULL)
   {
      usage();
      return 2;
   }
   setvbuf (stdout, NULL, _IOLBF, 0);
   read_serial_number (config.interface, serial, sizeof (serial));
   config.serial_number = serial;

   /* The cyclic thread must not wait for a low-priority thread. */
   pthread_mutexattr_init (&attr);
   pthread_mutexattr_setprotocol (&attr, PTHREAD_PRIO_INHERIT);
   pthread_mutex_init (&mutex, &attr);

   listener = listen_on (socket_path);
   if (listener < 0)
   {
      perror (socket_path);
      return 1;
   }
   if (pn_device_start (&config) != 0)
   {
      fprintf (stderr, "p-net could not be started on %s\n", config.interface);
      return 1;
   }
   if (pthread_create (&thread, NULL, node_thread, &listener) != 0)
   {
      perror ("node thread");
      return 1;
   }
   if (mlockall (MCL_CURRENT | MCL_FUTURE) != 0)
   {
      perror ("warning: memory is not locked");
   }
   errno = pthread_setschedparam (pthread_self(), SCHED_FIFO, &priority);
   if (errno != 0)
   {
      perror ("warning: no real-time priority");
   }

   clock_gettime (CLOCK_MONOTONIC, &next);
   for (;;)
   {
      next.tv_nsec += PN_TICK_US * 1000;
      if (next.tv_nsec >= 1000000000)
      {
         next.tv_nsec -= 1000000000;
         next.tv_sec++;
      }
      while (clock_nanosleep (CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) == EINTR)
      {
      }
      pn_device_tick();
   }
}
