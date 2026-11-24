/* SPDX-License-Identifier: GPL-3.0-only */
#include "state.h"

#include <string.h>

static bool has_data (uint16_t slot)
{
   return slot > PN_STATUS_SLOT && slot <= PN_MAX_SLOT;
}

void pn_state_node_lost (pn_state_t * s)
{
   uint16_t i;

   /* The layout is kept, so that a connecting PLC is still told about wrong
    * modules while the node restarts. */
   s->node_connected = false;
   for (i = 0; i <= PN_MAX_SLOT; i++)
   {
      s->slot[i].in_valid = false;
   }
}

void pn_state_plc_lost (pn_state_t * s)
{
   uint16_t i;

   s->plc_connected = false;
   s->plc_running = false;
   for (i = 0; i <= PN_MAX_SLOT; i++)
   {
      s->slot[i].plc_ident = 0;
      s->slot[i].plugged = 0;
      s->slot[i].plc_sizes_ok = false;
      s->slot[i].out_valid = false;
   }
}

pn_slot_state_t pn_state_slot (const pn_state_t * s, uint16_t slot)
{
   const pn_slot_t * p;

   if (!has_data (slot))
   {
      return PN_NOT_IN_PLC;
   }
   p = &s->slot[slot];
   if (p->plc_ident == 0)
   {
      return PN_NOT_IN_PLC;
   }
   return (p->plc_ident == p->node_ident && p->plc_sizes_ok) ? PN_MATCH : PN_MISMATCH;
}

/* The module of the node if it has one, else whatever the PLC expects. */
static uint32_t present_module (const pn_slot_t * p)
{
   return p->node_ident != 0 ? p->node_ident : p->plc_ident;
}

uint32_t pn_state_plc_expects (pn_state_t * s, uint16_t slot, uint32_t ident)
{
   if (slot == PN_STATUS_SLOT)
   {
      return PN_STATUS_IDENT;
   }
   if (!has_data (slot))
   {
      return ident;
   }
   s->slot[slot].plc_ident = ident;
   s->slot[slot].plc_sizes_ok = false;
   s->slot[slot].out_valid = false;
   s->slot[slot].plugged = present_module (&s->slot[slot]);
   return s->slot[slot].plugged;
}

bool pn_state_must_reconnect (const pn_state_t * s)
{
   uint16_t i;

   for (i = PN_STATUS_SLOT + 1; i <= PN_MAX_SLOT; i++)
   {
      const pn_slot_t * p = &s->slot[i];

      if (p->plc_ident != 0 && p->plugged != present_module (p))
      {
         return true;
      }
   }
   return false;
}

void pn_state_plc_sizes (pn_state_t * s, uint16_t slot, uint16_t in, uint16_t out)
{
   const pn_module_t * m;

   if (!has_data (slot))
   {
      return;
   }
   m = pn_module_find (s->slot[slot].plc_ident);
   s->slot[slot].plc_sizes_ok =
      m != NULL && in == (m->direction == PN_IN ? m->size : 0) &&
      out == (m->direction == PN_OUT ? m->size : 0);
}

static void status_module (const pn_state_t * s, uint8_t * data)
{
   uint16_t word = 0;
   uint16_t i;

   if (s->node_connected)
   {
      word |= PN_STATE_NODE_CONNECTED;
   }
   for (i = PN_STATUS_SLOT + 1; i <= PN_MAX_SLOT; i++)
   {
      const pn_slot_t * p = &s->slot[i];
      const pn_module_t * m = pn_module_find (p->node_ident);

      switch (pn_state_slot (s, i))
      {
      case PN_MISMATCH:
         word |= PN_STATE_MISMATCH;
         break;
      case PN_NOT_IN_PLC:
         if (m != NULL)
         {
            word |= PN_STATE_MISSING;
         }
         break;
      case PN_MATCH:
         if (m != NULL && m->direction == PN_IN && !p->in_valid)
         {
            word |= PN_STATE_IN_INVALID;
         }
         break;
      }
   }
   data[0] = (uint8_t)(s->mapping_id >> 24);
   data[1] = (uint8_t)(s->mapping_id >> 16);
   data[2] = (uint8_t)(s->mapping_id >> 8);
   data[3] = (uint8_t)s->mapping_id;
   data[4] = (uint8_t)(word >> 8);
   data[5] = (uint8_t)word;
   data[6] = (uint8_t)(s->heartbeat >> 8);
   data[7] = (uint8_t)s->heartbeat;
}

bool pn_state_get_input (const pn_state_t * s, uint16_t slot, uint8_t * data)
{
   memset (data, 0, PN_MAX_MODULE_SIZE);
   if (slot == PN_STATUS_SLOT)
   {
      status_module (s, data);
      return true;
   }
   if (pn_state_slot (s, slot) != PN_MATCH)
   {
      return false;
   }
   memcpy (data, s->slot[slot].in, PN_MAX_MODULE_SIZE);
   return s->node_connected && s->slot[slot].in_valid;
}

void pn_state_set_output (pn_state_t * s, uint16_t slot, const uint8_t * data, bool valid)
{
   if (!has_data (slot))
   {
      return;
   }
   if (valid)
   {
      memcpy (s->slot[slot].out, data, PN_MAX_MODULE_SIZE);
   }
   s->slot[slot].out_valid = valid;
}

bool pn_state_output_used (const pn_state_t * s, uint16_t slot)
{
   return s->node_connected && pn_state_slot (s, slot) == PN_MATCH;
}
