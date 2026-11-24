/* SPDX-License-Identifier: GPL-3.0-only */
/* The PROFINET side: connects the state to p-net. */
#ifndef DEVICE_H
#define DEVICE_H

#include "state.h"

#define PN_TICK_US 1000

typedef struct
{
   uint16_t vendor_id;         /* as in the GSDML file */
   uint16_t device_id;
   const char * interface;
   const char * station_name;
   const char * storage;       /* directory for p-net's persistent data */
   const char * serial_number;
   void (*lock) (void);        /* protects state */
   void (*unlock) (void);
   pn_state_t * state;
} pn_device_config_t;

/* Returns 0, or -1 if p-net could not be started. */
int pn_device_start (const pn_device_config_t * config);

/* Call every PN_TICK_US from one thread. */
void pn_device_tick (void);

#endif
