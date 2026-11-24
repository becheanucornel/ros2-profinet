/* SPDX-License-Identifier: GPL-3.0-only */
/* The module catalog, as fixed by the GSDML file. */
#ifndef MODULES_H
#define MODULES_H

#include <stdint.h>

#define PN_MAX_SLOT        32
#define PN_STATUS_SLOT     1
#define PN_STATUS_IDENT    0x1001
#define PN_SUBMODULE_IDENT 1
#define PN_MAX_MODULE_SIZE 64

typedef enum { PN_IN, PN_OUT } pn_direction_t; /* IN = to the PLC */

typedef struct
{
   uint32_t ident;
   pn_direction_t direction;
   uint8_t size;
} pn_module_t;

/* Returns NULL if the GSDML has no such module. */
const pn_module_t * pn_module_find (uint32_t ident);

#endif
