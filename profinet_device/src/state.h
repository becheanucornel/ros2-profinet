/* SPDX-License-Identifier: GPL-3.0-only */
/* Process data and slot state, shared by the PROFINET and the node side.
 * Portable and free of locks: callers must hold the state lock. */
#ifndef STATE_H
#define STATE_H

#include "modules.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum { PN_NOT_IN_PLC = 0, PN_MATCH = 1, PN_MISMATCH = 2 } pn_slot_state_t;

typedef struct
{
   uint32_t node_ident; /* module in config_profinet.xml, 0 = none */
   uint32_t plc_ident;  /* module in the PLC project, 0 = none */
   uint32_t plugged;    /* module the PLC was told is present, 0 = none */
   bool plc_sizes_ok;   /* the PLC's data length is the one in the catalog */
   bool in_valid;
   bool out_valid;
   uint8_t in[PN_MAX_MODULE_SIZE];
   uint8_t out[PN_MAX_MODULE_SIZE];
} pn_slot_t;

typedef struct
{
   pn_slot_t slot[PN_MAX_SLOT + 1]; /* index 0 and the status slot hold no data */
   uint32_t mapping_id;
   uint16_t heartbeat;
   bool node_connected;
   bool plc_connected;
   bool plc_running;
} pn_state_t;

/* Bits of the state word in the status module. */
#define PN_STATE_NODE_CONNECTED 0x0001
#define PN_STATE_MISMATCH       0x0002 /* a slot has different modules */
#define PN_STATE_IN_INVALID     0x0004 /* data of an IN slot is stale or bad */
#define PN_STATE_MISSING        0x0008 /* a mapped slot is not in the PLC */

void pn_state_node_lost (pn_state_t * s);
void pn_state_plc_lost (pn_state_t * s);

pn_slot_state_t pn_state_slot (const pn_state_t * s, uint16_t slot);

/* The PLC expects this module. Returns the ident that is really present. */
uint32_t pn_state_plc_expects (pn_state_t * s, uint16_t slot, uint32_t ident);
void pn_state_plc_sizes (pn_state_t * s, uint16_t slot, uint16_t in, uint16_t out);

/* True if the PLC was told about other modules than the node now has, because
 * the node started or changed its layout later. Only reconnecting solves it. */
bool pn_state_must_reconnect (const pn_state_t * s);

/* Copies PN_MAX_MODULE_SIZE bytes. Returns true if the data is good. */
bool pn_state_get_input (const pn_state_t * s, uint16_t slot, uint8_t * data);
void pn_state_set_output (pn_state_t * s, uint16_t slot, const uint8_t * data, bool valid);

/* True if output data for this slot has a consumer. */
bool pn_state_output_used (const pn_state_t * s, uint16_t slot);

#endif
