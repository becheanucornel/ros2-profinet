/* SPDX-License-Identifier: GPL-3.0-only */
/* Messages to and from the ROS 2 node. The format is described in
 * profinet_bridge/protocol.py. */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "state.h"

#include <stddef.h>

#define PROTOCOL_MAX_PACKET 4096

typedef enum
{
   PROTOCOL_OK = 0,
   PROTOCOL_REJECT_VERSION = 1,
   PROTOCOL_REJECT_LAYOUT = 2,
   PROTOCOL_REJECT_BUSY = 3,
   PROTOCOL_REJECT_MALFORMED = 4
} protocol_result_t;

/* Applies a packet from the node. A refused packet changes nothing. */
protocol_result_t protocol_receive (pn_state_t * s, const uint8_t * packet, size_t length);

/* Both return the packet length. buffer must have PROTOCOL_MAX_PACKET bytes. */
size_t protocol_outputs (const pn_state_t * s, uint8_t * buffer);
size_t protocol_reject (protocol_result_t reason, uint8_t * buffer);

#endif
