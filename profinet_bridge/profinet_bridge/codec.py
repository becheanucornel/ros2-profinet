# SPDX-License-Identifier: Apache-2.0
"""Convert between Python values and PROFINET bytes (big-endian)."""
import math
import struct
from dataclasses import dataclass

# encoding name: (struct format, size)
ENCODINGS = {
    "BOOL": (None, 1),
    "U8": (">B", 1), "I8": (">b", 1),
    "U16": (">H", 2), "I16": (">h", 2),
    "U32": (">I", 4), "I32": (">i", 4),
    "U64": (">Q", 8), "I64": (">q", 8),
    "F32": (">f", 4), "F64": (">d", 8),
}
MODULE_ENCODING = {"BOOL": "BOOL", "INT": "I16", "DINT": "I32", "REAL": "F32"}


class CodecError(ValueError):
    """The value cannot be represented, so it must not be sent as valid data."""


@dataclass(frozen=True)
class Location:
    encoding: str
    offset: int
    bit: int = 0

    @property
    def size(self):
        return ENCODINGS[self.encoding][1]


def write(buffer, loc, value):
    if not isinstance(value, (bool, int, float)):
        raise CodecError(f"{value!r} is not a number")
    if isinstance(value, bool) != (loc.encoding == "BOOL"):
        raise CodecError(f"{value!r} cannot be sent as {loc.encoding}")
    if loc.encoding == "BOOL":
        mask = 1 << loc.bit
        if value:
            buffer[loc.offset] |= mask
        else:
            buffer[loc.offset] &= ~mask & 0xFF
        return
    if isinstance(value, float) and not math.isfinite(value):
        raise CodecError(f"{value} is not a finite number")
    if loc.encoding not in ("F32", "F64"):
        value = round(value)
    try:
        struct.pack_into(ENCODINGS[loc.encoding][0], buffer, loc.offset, value)
    except (struct.error, OverflowError):
        raise CodecError(f"{value} does not fit in {loc.encoding}") from None


def read(buffer, loc):
    if loc.encoding == "BOOL":
        return bool(buffer[loc.offset] >> loc.bit & 1)
    return struct.unpack_from(ENCODINGS[loc.encoding][0], buffer, loc.offset)[0]
