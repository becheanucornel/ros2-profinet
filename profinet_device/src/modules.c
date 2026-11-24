/* SPDX-License-Identifier: GPL-3.0-only */
#include "modules.h"

#include <stddef.h>

static const pn_module_t modules[] = {
   {0x1001, PN_IN, 8}, /* node status */
   /* BOOL */
   {0x1108, PN_IN, 1},
   {0x1110, PN_IN, 2},
   {0x1120, PN_IN, 4},
   {0x2108, PN_OUT, 1},
   {0x2110, PN_OUT, 2},
   {0x2120, PN_OUT, 4},
   /* INT */
   {0x1201, PN_IN, 2},
   {0x1202, PN_IN, 4},
   {0x1204, PN_IN, 8},
   {0x1208, PN_IN, 16},
   {0x2201, PN_OUT, 2},
   {0x2202, PN_OUT, 4},
   {0x2204, PN_OUT, 8},
   {0x2208, PN_OUT, 16},
   /* DINT */
   {0x1301, PN_IN, 4},
   {0x1302, PN_IN, 8},
   {0x1304, PN_IN, 16},
   {0x1308, PN_IN, 32},
   {0x2301, PN_OUT, 4},
   {0x2302, PN_OUT, 8},
   {0x2304, PN_OUT, 16},
   {0x2308, PN_OUT, 32},
   /* REAL */
   {0x1401, PN_IN, 4},
   {0x1402, PN_IN, 8},
   {0x1404, PN_IN, 16},
   {0x1408, PN_IN, 32},
   {0x1410, PN_IN, 64},
   {0x2401, PN_OUT, 4},
   {0x2402, PN_OUT, 8},
   {0x2404, PN_OUT, 16},
   {0x2408, PN_OUT, 32},
   {0x2410, PN_OUT, 64},
   /* Raw bytes */
   {0x1508, PN_IN, 8},
   {0x1510, PN_IN, 16},
   {0x1520, PN_IN, 32},
   {0x1540, PN_IN, 64},
   {0x2508, PN_OUT, 8},
   {0x2510, PN_OUT, 16},
   {0x2520, PN_OUT, 32},
   {0x2540, PN_OUT, 64},
};

const pn_module_t * pn_module_find (uint32_t ident)
{
   size_t i;

   for (i = 0; i < sizeof (modules) / sizeof (modules[0]); i++)
   {
      if (modules[i].ident == ident)
      {
         return &modules[i];
      }
   }
   return NULL;
}
