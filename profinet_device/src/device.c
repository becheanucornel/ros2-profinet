/* SPDX-License-Identifier: GPL-3.0-only */
/* p-net calls the callbacks below from its receive thread or from
 * pn_device_tick(). They only record what happened, plug modules and set initial
 * data; everything else is done in pn_device_tick().
 *
 * Two locks, always taken in this order: pnal_lock() for p-net, which is not safe
 * to use from two threads (see third_party/patches), and the state lock, which is
 * never held while calling p-net. */
#include "device.h"

#include "device_identity.h"

#include <pnet_api.h>

#include <stdio.h>
#include <string.h>

#define API              PNET_API_NO_APPLICATION_PROFILE
#define SUBSLOT          1
#define THREAD_STACK     (256 * 1024)
#define READY_MAX_TICKS  1000
#define STARTUP_MAX_TICKS 20000 /* a PLC that vanished must not block a new connection */
#define HIGH(x)          ((uint8_t)((x) >> 8))
#define LOW(x)           ((uint8_t)((x)&0xFF))

typedef struct
{
   uint16_t in_size;
   uint16_t out_size;
   uint8_t iocs;
   bool plugged;
} slot_io_t;

static struct
{
   pnet_t * net;
   pn_device_config_t config;

   /* Protected by the state lock. */
   slot_io_t io[PN_MAX_SLOT + 1];
   bool connected;         /* an AR exists */
   uint32_t arep;
   bool ready_pending;     /* pnet_application_ready() must be called */
   unsigned ready_ticks;
   unsigned startup_ticks;
   bool pull_pending;      /* modules of the last connection must be removed */
   bool abort_pending;     /* a PLC connected before they were removed */
   bool alarm_pending;
   pnet_alarm_argument_t alarm;
} dev;

static void lock (void)
{
   dev.config.lock();
}

static void unlock (void)
{
   dev.config.unlock();
}

/******************************* Process data ********************************/

/* Writes one slot's input data and returns p-net's result. */
static int write_input (uint16_t slot, uint16_t size)
{
   uint8_t data[PN_MAX_MODULE_SIZE];
   bool good;

   lock();
   good = pn_state_get_input (dev.config.state, slot, data);
   unlock();

   if (size > sizeof (data))
   {
      /* Not a module of ours: the PLC uses another GSDML file. */
      static const uint8_t zero[1440] = {0}; /* the largest possible frame */
      return pnet_input_set_data_and_iops (
         dev.net,
         API,
         slot,
         SUBSLOT,
         zero,
         size <= sizeof (zero) ? size : sizeof (zero),
         PNET_IOXS_BAD);
   }
   return pnet_input_set_data_and_iops (
      dev.net,
      API,
      slot,
      SUBSLOT,
      data,
      size,
      good ? PNET_IOXS_GOOD : PNET_IOXS_BAD);
}

static void read_output (uint16_t slot, uint16_t size)
{
   uint8_t data[PN_MAX_MODULE_SIZE] = {0};
   uint16_t length = sizeof (data);
   uint8_t iops = PNET_IOXS_BAD;
   bool is_new = false;
   bool valid;

   if (size > sizeof (data))
   {
      return;
   }
   valid = pnet_output_get_data_and_iops (
              dev.net,
              API,
              slot,
              SUBSLOT,
              &is_new,
              data,
              &length,
              &iops) == 0 &&
           length == size && iops == PNET_IOXS_GOOD;
   lock();
   pn_state_set_output (
      dev.config.state,
      slot,
      data,
      valid && dev.config.state->plc_connected);
   unlock();
}

/* Tells the PLC whether its output data is used. */
static void write_iocs (uint16_t slot, bool always)
{
   uint8_t iocs;
   bool changed;

   lock();
   iocs = pn_state_output_used (dev.config.state, slot) ? PNET_IOXS_GOOD : PNET_IOXS_BAD;
   changed = iocs != dev.io[slot].iocs;
   dev.io[slot].iocs = iocs;
   unlock();
   if (changed || always)
   {
      (void)pnet_output_set_iocs (dev.net, API, slot, SUBSLOT, iocs);
   }
}

static void exchange (bool initial)
{
   uint16_t slot;

   for (slot = 1; slot <= PN_MAX_SLOT; slot++)
   {
      slot_io_t io;

      lock();
      io = dev.io[slot];
      unlock();
      if (!io.plugged)
      {
         continue;
      }
      if (io.in_size > 0)
      {
         (void)write_input (slot, io.in_size);
      }
      if (io.out_size > 0)
      {
         write_iocs (slot, initial);
         if (!initial)
         {
            read_output (slot, io.out_size);
         }
      }
   }
}

/********************************* Callbacks *********************************/

static int on_exp_module (
   pnet_t * net,
   void * arg,
   uint32_t api,
   uint16_t slot,
   uint32_t ident)
{
   uint32_t present;

   if (slot > PN_MAX_SLOT)
   {
      return -1;
   }
   lock();
   present = pn_state_plc_expects (dev.config.state, slot, ident);
   unlock();
   if (present != ident)
   {
      printf (
         "slot %u: the PLC expects module 0x%04X, but 0x%04X is configured\n",
         slot,
         (unsigned)ident,
         (unsigned)present);
   }
   /* p-net reports a wrong module to the PLC if present is not ident. */
   return pnet_plug_module (net, api, slot, present);
}

static int on_exp_submodule (
   pnet_t * net,
   void * arg,
   uint32_t api,
   uint16_t slot,
   uint16_t subslot,
   uint32_t module_ident,
   uint32_t submodule_ident,
   const pnet_data_cfg_t * expected)
{
   int ret;

   if (slot > PN_MAX_SLOT)
   {
      return -1;
   }
   /* Always use the sizes the PLC expects, so that the frame layout is right
    * for the other slots. The data of a wrong module is marked as bad. */
   ret = pnet_plug_submodule (
      net,
      api,
      slot,
      subslot,
      module_ident,
      submodule_ident,
      expected->data_dir,
      expected->insize,
      expected->outsize);
   if (slot >= 1 && subslot == SUBSLOT)
   {
      lock();
      pn_state_plc_sizes (dev.config.state, slot, expected->insize, expected->outsize);
      dev.io[slot].plugged = ret == 0;
      dev.io[slot].in_size = expected->insize;
      dev.io[slot].out_size = expected->outsize;
      dev.io[slot].iocs = PNET_IOXS_BAD;
      unlock();
   }
   return ret;
}

static void set_initial_data (pnet_t * net)
{
   static const uint16_t dap[] = {
      PNET_SUBSLOT_DAP_IDENT,
      PNET_SUBSLOT_DAP_INTERFACE_1_IDENT,
      PNET_SUBSLOT_DAP_INTERFACE_1_PORT_1_IDENT};
   size_t i;

   for (i = 0; i < sizeof (dap) / sizeof (dap[0]); i++)
   {
      (void)pnet_input_set_data_and_iops (
         net,
         API,
         PNET_SLOT_DAP_IDENT,
         dap[i],
         NULL,
         0,
         PNET_IOXS_GOOD);
   }
   exchange (true);
   (void)pnet_set_provider_state (net, true);
}

static int on_state (pnet_t * net, void * arg, uint32_t arep, pnet_event_values_t event)
{
   uint16_t err_cls = 0;
   uint16_t err_code = 0;

   switch (event)
   {
   case PNET_EVENT_STARTUP:
      lock();
      dev.connected = true;
      dev.startup_ticks = 0;
      dev.arep = arep;
      dev.abort_pending = dev.pull_pending;
      unlock();
      break;

   case PNET_EVENT_PRMEND:
      set_initial_data (net);
      lock();
      dev.arep = arep;
      dev.ready_pending = true;
      dev.ready_ticks = 0;
      unlock();
      break;

   case PNET_EVENT_DATA:
      lock();
      dev.config.state->plc_connected = true;
      unlock();
      printf ("PLC connected\n");
      break;

   case PNET_EVENT_ABORT:
      (void)pnet_get_ar_error_codes (net, arep, &err_cls, &err_code);
      lock();
      pn_state_plc_lost (dev.config.state);
      dev.connected = false;
      dev.ready_pending = false;
      dev.alarm_pending = false;
      dev.abort_pending = false;
      dev.pull_pending = true;
      unlock();
      printf ("PLC disconnected (error class %u, code %u)\n", err_cls, err_code);
      break;

   default:
      break;
   }
   return 0;
}

static int on_new_data_status (
   pnet_t * net,
   void * arg,
   uint32_t arep,
   uint32_t crep,
   uint8_t changes,
   uint8_t status)
{
   const uint8_t needed = (1 << PNET_DATA_STATUS_BIT_PROVIDER_STATE) |
                          (1 << PNET_DATA_STATUS_BIT_DATA_VALID);

   lock();
   dev.config.state->plc_running = (status & needed) == needed;
   unlock();
   return 0;
}

static int on_alarm (
   pnet_t * net,
   void * arg,
   uint32_t arep,
   const pnet_alarm_argument_t * alarm,
   uint16_t data_len,
   uint16_t data_usi,
   const uint8_t * data)
{
   lock();
   dev.alarm = *alarm;
   dev.alarm_pending = true;
   unlock();
   return 0;
}

static int on_read (
   pnet_t * net,
   void * arg,
   uint32_t arep,
   uint32_t api,
   uint16_t slot,
   uint16_t subslot,
   uint16_t idx,
   uint16_t sequence_number,
   uint8_t ** data,
   uint16_t * length,
   pnet_result_t * result)
{
   result->pnio_status.error_code = PNET_ERROR_CODE_READ;
   result->pnio_status.error_decode = PNET_ERROR_DECODE_PNIORW;
   result->pnio_status.error_code_1 = PNET_ERROR_CODE_1_APP_READ_ERROR;
   result->pnio_status.error_code_2 = 0;
   return -1; /* the device has no records of its own */
}

static int on_write (
   pnet_t * net,
   void * arg,
   uint32_t arep,
   uint32_t api,
   uint16_t slot,
   uint16_t subslot,
   uint16_t idx,
   uint16_t sequence_number,
   uint16_t length,
   const uint8_t * data,
   pnet_result_t * result)
{
   result->pnio_status.error_code = PNET_ERROR_CODE_WRITE;
   result->pnio_status.error_decode = PNET_ERROR_DECODE_PNIORW;
   result->pnio_status.error_code_1 = PNET_ERROR_CODE_1_APP_WRITE_ERROR;
   result->pnio_status.error_code_2 = 0;
   return -1;
}

static int on_signal_led (pnet_t * net, void * arg, bool on)
{
   printf ("signal LED %s\n", on ? "on" : "off");
   return 0;
}

/*********************************** Tick ************************************/

void pn_device_tick (void)
{
   pnet_alarm_argument_t alarm;
   bool ready, pull, abort_ar, ack, connected, reconnect;
   uint32_t arep;
   uint16_t slot;

   pnal_lock();
   lock();
   abort_ar = dev.abort_pending;
   dev.abort_pending = false;
   /* Never pull modules of a live connection. */
   pull = dev.pull_pending && !dev.connected;
   ready = dev.ready_pending;
   ack = dev.alarm_pending;
   dev.alarm_pending = false;
   alarm = dev.alarm;
   arep = dev.arep;
   connected = dev.connected;
   reconnect = dev.config.state->plc_connected && dev.config.state->node_connected &&
               pn_state_must_reconnect (dev.config.state);
   if (connected && !dev.config.state->plc_connected &&
       ++dev.startup_ticks == STARTUP_MAX_TICKS)
   {
      abort_ar = true;
   }
   unlock();

   if (reconnect)
   {
      printf ("the node has other modules than the PLC was told: reconnecting\n");
      (void)pnet_ar_abort (dev.net, arep);
      connected = false;
   }
   else if (abort_ar)
   {
      /* The PLC may have been given the old modules, or it vanished while
       * connecting. If it is there, it reconnects by itself. */
      (void)pnet_ar_abort (dev.net, arep);
   }
   if (pull)
   {
      /* p-net asks for the modules again only if the slots are empty. Without
       * this, a changed PLC project would not be noticed. */
      for (slot = 1; slot <= PN_MAX_SLOT; slot++)
      {
         (void)pnet_pull_module (dev.net, API, slot);
      }
      lock();
      memset (dev.io, 0, sizeof (dev.io));
      dev.pull_pending = false;
      unlock();
   }
   if (ready)
   {
      bool done = pnet_application_ready (dev.net, arep) == 0;
      bool give_up;

      lock();
      give_up = !done && ++dev.ready_ticks >= READY_MAX_TICKS;
      if (done || give_up)
      {
         dev.ready_pending = false;
      }
      unlock();
      if (give_up)
      {
         printf ("giving up: not all data could be set for the PLC\n");
      }
   }
   if (ack)
   {
      static const pnet_pnio_status_t ok = {0, 0, 0, 0};

      (void)pnet_alarm_send_ack (dev.net, arep, &alarm, &ok);
   }
   if (connected)
   {
      exchange (false);
   }
   pnet_handle_periodic (dev.net);
   pnal_unlock();
}

/*********************************** Start ***********************************/

static int start_stack (const pnet_cfg_t * cfg)
{
   bool ok;

   /* Frames that arrive before the device is complete must wait. */
   pnal_lock();
   dev.net = pnet_init (cfg);
   ok = dev.net != NULL &&
        pnet_plug_module (dev.net, API, PNET_SLOT_DAP_IDENT, PNET_MOD_DAP_IDENT) == 0 &&
        pnet_plug_submodule (
           dev.net,
           API,
           PNET_SLOT_DAP_IDENT,
           PNET_SUBSLOT_DAP_IDENT,
           PNET_MOD_DAP_IDENT,
           PNET_SUBMOD_DAP_IDENT,
           PNET_DIR_NO_IO,
           0,
           0) == 0 &&
        pnet_plug_submodule (
           dev.net,
           API,
           PNET_SLOT_DAP_IDENT,
           PNET_SUBSLOT_DAP_INTERFACE_1_IDENT,
           PNET_MOD_DAP_IDENT,
           PNET_SUBMOD_DAP_INTERFACE_1_IDENT,
           PNET_DIR_NO_IO,
           0,
           0) == 0 &&
        pnet_plug_submodule (
           dev.net,
           API,
           PNET_SLOT_DAP_IDENT,
           PNET_SUBSLOT_DAP_INTERFACE_1_PORT_1_IDENT,
           PNET_MOD_DAP_IDENT,
           PNET_SUBMOD_DAP_INTERFACE_1_PORT_1_IDENT,
           PNET_DIR_NO_IO,
           0,
           0) == 0;
   pnal_unlock();
   return ok ? 0 : -1;
}

int pn_device_start (const pn_device_config_t * config)
{
   static pnet_cfg_t cfg; /* p-net keeps a pointer to it */
   pnet_im_0_t * im0 = &cfg.im_0_data;

   dev.config = *config;

   cfg.tick_us = PN_TICK_US;
   cfg.state_cb = on_state;
   cfg.read_cb = on_read;
   cfg.write_cb = on_write;
   cfg.exp_module_cb = on_exp_module;
   cfg.exp_submodule_cb = on_exp_submodule;
   cfg.new_data_status_cb = on_new_data_status;
   cfg.alarm_ind_cb = on_alarm;
   cfg.signal_led_cb = on_signal_led;

   im0->im_vendor_id_hi = HIGH (config->vendor_id);
   im0->im_vendor_id_lo = LOW (config->vendor_id);
   snprintf (im0->im_order_id, sizeof (im0->im_order_id), "%s", PN_ORDER_ID);
   snprintf (
      im0->im_serial_number,
      sizeof (im0->im_serial_number),
      "%s",
      config->serial_number);
   im0->im_hardware_revision = 1;
   im0->im_sw_revision_prefix = 'V';
   im0->im_sw_revision_functional_enhancement = 0;
   im0->im_sw_revision_bug_fix = 1;
   im0->im_sw_revision_internal_change = 0;
   im0->im_version_major = 1;
   im0->im_version_minor = 1;
   im0->im_supported = PNET_SUPPORTED_IM1 | PNET_SUPPORTED_IM2 | PNET_SUPPORTED_IM3;

   cfg.device_id.vendor_id_hi = HIGH (config->vendor_id);
   cfg.device_id.vendor_id_lo = LOW (config->vendor_id);
   cfg.device_id.device_id_hi = HIGH (config->device_id);
   cfg.device_id.device_id_lo = LOW (config->device_id);
   cfg.oem_device_id = cfg.device_id;
   snprintf (cfg.station_name, sizeof (cfg.station_name), "%s", config->station_name);
   snprintf (cfg.product_name, sizeof (cfg.product_name), "%s", PN_PRODUCT_NAME);
   snprintf (cfg.file_directory, sizeof (cfg.file_directory), "%s", config->storage);
   cfg.min_device_interval = PN_MIN_DEVICE_INTERVAL;
   cfg.send_hello = true;
   cfg.num_physical_ports = 1;
   cfg.if_cfg.main_netif_name = config->interface;
   cfg.if_cfg.physical_ports[0].netif_name = config->interface;
   cfg.if_cfg.physical_ports[0].default_mau_type = 0x0010; /* 100 Mbit/s full duplex */
   cfg.pnal_cfg.snmp_thread.prio = 1;
   cfg.pnal_cfg.snmp_thread.stack_size = THREAD_STACK;
   cfg.pnal_cfg.eth_recv_thread.prio = 10;
   cfg.pnal_cfg.eth_recv_thread.stack_size = THREAD_STACK;
   cfg.pnal_cfg.bg_worker_thread.prio = 5;
   cfg.pnal_cfg.bg_worker_thread.stack_size = THREAD_STACK;

   return start_stack (&cfg);
}
