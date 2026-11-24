# SPDX-License-Identifier: Apache-2.0
"""The module catalog, as fixed by the GSDML file.

Every name, ident and size here must match gsdml/*.xml.
"""
from dataclasses import dataclass

MAX_SLOT = 32
STATUS_SLOT = 1
MAX_IO_BYTES = 1440  # per direction, status bytes included

# type name: (code used in the ident, bytes per element, counts in the catalog)
# BOOL counts are bits, so they are handled separately below.
_TYPES = {
    "BOOL": (1, None, (8, 16, 32)),
    "INT": (2, 2, (1, 2, 4, 8)),
    "DINT": (3, 4, (1, 2, 4, 8)),
    "REAL": (4, 4, (1, 2, 4, 8, 16)),
    "RAW": (5, 1, (8, 16, 32, 64)),
}


@dataclass(frozen=True)
class Module:
    name: str
    ident: int
    type: str
    count: int
    direction: str  # "IN" = to the PLC, "OUT" = from the PLC
    size: int       # bytes


def _build():
    modules = {}
    for code, direction in ((1, "IN"), (2, "OUT")):
        for type_name, (type_code, width, counts) in _TYPES.items():
            for count in counts:
                name = f"{type_name}_{count}_{direction}"
                size = count // 8 if width is None else count * width
                ident = code << 12 | type_code << 8 | count
                modules[name] = Module(name, ident, type_name, count, direction, size)
    return modules


MODULES = _build()
