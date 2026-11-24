# SPDX-License-Identifier: Apache-2.0
"""Messages between this node and profinet_device, over a Unix SOCK_SEQPACKET socket.

Each packet is one complete message. Headers are little-endian; process data is
passed through unchanged (big-endian, as PROFINET defines it).

  header   magic 'PNIP', version u8, type u8, entry count u16
  HELLO    mapping_id u32, then per slot: slot u16, reserved u16, module ident u32
  INPUTS   heartbeat u16, reserved u16, then per IN slot:
           slot u16, valid u8, length u8, data
  OUTPUTS  plc_connected u8, plc_running u8, reserved u16, then per mapped slot:
           slot u16, state u8, valid u8, length u8, reserved u8, data
  REJECT   reason u32 (the HELLO was refused; count is 0)

The same layout is implemented in profinet_device/src/protocol.c.
"""
import struct
from dataclasses import dataclass

VERSION = 1
MAX_PACKET = 4096
HELLO, INPUTS, OUTPUTS, REJECT = 1, 2, 3, 4
NOT_IN_PLC, MATCH, MISMATCH = 0, 1, 2
REJECT_REASONS = {1: "protocol version differs", 2: "unknown slot or module",
                  3: "another node is already connected", 4: "malformed message"}

_HEADER = struct.Struct("<4sBBH")
_MAGIC = b"PNIP"


class ProtocolError(ValueError):
    pass


@dataclass(frozen=True)
class SlotOutput:
    state: int
    valid: bool
    data: bytes


@dataclass(frozen=True)
class Outputs:
    plc_connected: bool
    plc_running: bool
    slots: dict


def encode_hello(config):
    packet = _HEADER.pack(_MAGIC, VERSION, HELLO, len(config.slots))
    packet += struct.pack("<I", config.mapping_id)
    for s in config.slots:
        packet += struct.pack("<HHI", s.number, 0, s.module.ident)
    return packet


def encode_inputs(heartbeat, images):
    """images: (slot, valid, data) for every IN slot."""
    images = list(images)
    packet = _HEADER.pack(_MAGIC, VERSION, INPUTS, len(images))
    packet += struct.pack("<HH", heartbeat & 0xFFFF, 0)
    for slot, valid, data in images:
        packet += struct.pack("<HBB", slot, valid, len(data)) + bytes(data)
    return packet


def decode(packet):
    """Return an Outputs, or a str with the reason of a REJECT."""
    try:
        magic, version, kind, count = _HEADER.unpack_from(packet)
        if magic != _MAGIC:
            raise ProtocolError("not a profinet_device message")
        if version != VERSION:
            raise ProtocolError(f"profinet_device uses protocol version {version}, not {VERSION}")
        pos = _HEADER.size
        if kind == REJECT:
            reason, = struct.unpack_from("<I", packet, pos)
            return REJECT_REASONS.get(reason, f"reason {reason}")
        if kind != OUTPUTS:
            raise ProtocolError(f"unexpected message type {kind}")
        connected, running, _ = struct.unpack_from("<BBH", packet, pos)
        pos += 4
        slots = {}
        for _ in range(count):
            slot, state, valid, length, _ = struct.unpack_from("<HBBBB", packet, pos)
            pos += 6
            if pos + length > len(packet):
                raise ProtocolError("truncated message")
            slots[slot] = SlotOutput(state, bool(valid), bytes(packet[pos:pos + length]))
            pos += length
    except struct.error:
        raise ProtocolError("truncated message") from None
    if pos != len(packet):
        raise ProtocolError("message is too long")
    return Outputs(bool(connected), bool(running), slots)
