/* SPDX-License-Identifier: GPL-3.0-only */
#include "protocol.h"

#include <string.h>

#define VERSION     1
#define HEADER_SIZE 8

enum { HELLO = 1, INPUTS = 2, OUTPUTS = 3, REJECT = 4 };

static uint16_t get16 (const uint8_t * p)
{
   return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t get32 (const uint8_t * p)
{
   return get16 (p) | (uint32_t)get16 (p + 2) << 16;
}

static void put16 (uint8_t * p, uint16_t v)
{
   p[0] = (uint8_t)v;
   p[1] = (uint8_t)(v >> 8);
}

static void put_header (uint8_t * p, uint8_t type, uint16_t count)
{
   memcpy (p, "PNIP", 4);
   p[4] = VERSION;
   p[5] = type;
   put16 (p + 6, count);
}

static protocol_result_t receive_hello (
   pn_state_t * s,
   const uint8_t * p,
   size_t length,
   uint16_t count)
{
   uint32_t idents[PN_MAX_SLOT + 1] = {0};
   uint16_t i;

   if (length != 4 + (size_t)count * 8)
   {
      return PROTOCOL_REJECT_MALFORMED;
   }
   for (i = 0; i < count; i++)
   {
      const uint8_t * entry = p + 4 + (size_t)i * 8;
      uint16_t slot = get16 (entry);
      uint32_t ident = get32 (entry + 4);

      if (
         slot <= PN_STATUS_SLOT || slot > PN_MAX_SLOT || idents[slot] != 0 ||
         ident == PN_STATUS_IDENT || pn_module_find (ident) == NULL)
      {
         return PROTOCOL_REJECT_LAYOUT;
      }
      idents[slot] = ident;
   }

   pn_state_node_lost (s);
   for (i = 0; i <= PN_MAX_SLOT; i++)
   {
      s->slot[i].node_ident = idents[i];
   }
   s->mapping_id = get32 (p);
   s->node_connected = true;
   return PROTOCOL_OK;
}

/* Walks over the entries; applies them only if apply is set. */
static bool walk_inputs (
   pn_state_t * s,
   const uint8_t * p,
   size_t length,
   uint16_t count,
   bool apply)
{
   size_t pos = 4;
   uint16_t i;

   for (i = 0; i < count; i++)
   {
      const pn_module_t * m;
      uint16_t slot;
      uint8_t size;

      if (length < pos + 4)
      {
         return false;
      }
      slot = get16 (p + pos);
      size = p[pos + 3];
      if (slot > PN_MAX_SLOT || length < pos + 4 + size)
      {
         return false;
      }
      m = pn_module_find (s->slot[slot].node_ident);
      if (m == NULL || m->direction != PN_IN || m->size != size)
      {
         return false;
      }
      if (apply)
      {
         s->slot[slot].in_valid = p[pos + 2] != 0;
         memcpy (s->slot[slot].in, p + pos + 4, size);
      }
      pos += 4 + (size_t)size;
   }
   return pos == length;
}

static protocol_result_t receive_inputs (
   pn_state_t * s,
   const uint8_t * p,
   size_t length,
   uint16_t count)
{
   if (length < 4 || !s->node_connected || !walk_inputs (s, p, length, count, false))
   {
      return PROTOCOL_REJECT_MALFORMED;
   }
   (void)walk_inputs (s, p, length, count, true);
   s->heartbeat = get16 (p);
   return PROTOCOL_OK;
}

protocol_result_t protocol_receive (pn_state_t * s, const uint8_t * packet, size_t length)
{
   if (length < HEADER_SIZE || memcmp (packet, "PNIP", 4) != 0)
   {
      return PROTOCOL_REJECT_MALFORMED;
   }
   if (packet[4] != VERSION)
   {
      return PROTOCOL_REJECT_VERSION;
   }
   switch (packet[5])
   {
   case HELLO:
      return receive_hello (s, packet + HEADER_SIZE, length - HEADER_SIZE, get16 (packet + 6));
   case INPUTS:
      return receive_inputs (s, packet + HEADER_SIZE, length - HEADER_SIZE, get16 (packet + 6));
   default:
      return PROTOCOL_REJECT_MALFORMED;
   }
}

size_t protocol_outputs (const pn_state_t * s, uint8_t * buffer)
{
   size_t pos = HEADER_SIZE + 4;
   uint16_t count = 0;
   uint16_t slot;

   for (slot = PN_STATUS_SLOT + 1; slot <= PN_MAX_SLOT; slot++)
   {
      const pn_slot_t * p = &s->slot[slot];
      const pn_module_t * m = pn_module_find (p->node_ident);
      uint8_t size;

      if (m == NULL)
      {
         continue;
      }
      size = m->direction == PN_OUT ? m->size : 0;
      put16 (buffer + pos, slot);
      buffer[pos + 2] = (uint8_t)pn_state_slot (s, slot);
      buffer[pos + 3] =
         m->direction == PN_OUT ? p->out_valid : (s->node_connected && p->in_valid);
      buffer[pos + 4] = size;
      buffer[pos + 5] = 0;
      memcpy (buffer + pos + 6, p->out, size);
      pos += 6 + (size_t)size;
      count++;
   }
   put_header (buffer, OUTPUTS, count);
   buffer[HEADER_SIZE] = s->plc_connected;
   buffer[HEADER_SIZE + 1] = s->plc_running;
   put16 (buffer + HEADER_SIZE + 2, 0);
   return pos;
}

size_t protocol_reject (protocol_result_t reason, uint8_t * buffer)
{
   put_header (buffer, REJECT, 0);
   put16 (buffer + HEADER_SIZE, (uint16_t)reason);
   put16 (buffer + HEADER_SIZE + 2, 0);
   return HEADER_SIZE + 4;
}
