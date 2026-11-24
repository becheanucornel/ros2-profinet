# SPDX-License-Identifier: Apache-2.0
"""Read and validate config_profinet.xml."""
import re
import xml.etree.ElementTree as ET
import zlib
from dataclasses import dataclass
from typing import Optional

from .codec import ENCODINGS, MODULE_ENCODING, Location
from .modules import MAX_IO_BYTES, MAX_SLOT, MODULES, STATUS_SLOT, Module

ON_DISCONNECT = ("stop", "zero", "hold")
QOS = ("sensor", "default", "latched")
_ID = re.compile(r"0x[0-9a-fA-F]{4}")
_NAME_LABEL = re.compile(r"[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?")
_TYPE = re.compile(r"[a-zA-Z]\w*/msg/[a-zA-Z]\w*")
_TOPIC = re.compile(r"/?[a-zA-Z_]\w*(/[a-zA-Z_]\w*)*")
_PATH = re.compile(r"[a-zA-Z_]\w*(\[\d+\])?(\.[a-zA-Z_]\w*(\[\d+\])?)*")


class ConfigError(ValueError):
    """Lists every problem found, one per line."""


@dataclass(frozen=True)
class Field:
    path: str
    location: Location


@dataclass(frozen=True)
class Mapping:
    slot: int
    module: Module
    topic: str
    type: str
    fields: tuple
    qos: str
    timeout_s: Optional[float] = None   # IN: None means never stale
    on_disconnect: str = "stop"         # OUT
    rate_hz: Optional[float] = None     # OUT: None means publish on change


@dataclass(frozen=True)
class Slot:
    number: int
    module: Module
    mappings: tuple


@dataclass(frozen=True)
class Config:
    vendor_id: int
    device_id: int
    station_name: str
    interface: str
    cycle_s: float
    slots: tuple

    @property
    def mapping_id(self):
        """Changes whenever the meaning of any byte changes."""
        lines = [f"{s.number}|{s.module.name}" for s in self.slots]
        for s in self.slots:
            for m in s.mappings:
                for f in m.fields:
                    loc = f.location
                    lines.append(f"{s.number}|{m.topic}|{m.type}|{f.path}|"
                                 f"{loc.encoding}|{loc.offset}|{loc.bit}")
        return zlib.crc32("\n".join(sorted(lines)).encode()) or 1

    def io_bytes(self, direction):
        """Frame bytes used in one direction, status bytes included.

        Each module adds its data and one provider status byte in its own
        direction, and one consumer status byte in the other. The status module
        and the 3 submodules of the device itself are always present.
        """
        own = [s.module for s in self.slots if s.module.direction == direction]
        other = len(self.slots) - len(own)
        fixed = 8 + 1 + 3 if direction == "IN" else 1 + 3
        return sum(m.size + 1 for m in own) + other + fixed


class _Reader:
    def __init__(self):
        self.errors = []

    def error(self, where, text):
        self.errors.append(f"{where}: {text}")

    def attributes(self, element, where, required, optional=()):
        for name in element.attrib:
            if name not in required and name not in optional:
                self.error(where, f"unknown attribute '{name}'")
        for name in required:
            if name not in element.attrib:
                self.error(where, f"missing attribute '{name}'")
        return element.attrib

    def children(self, element, where, tag):
        for child in element:
            if child.tag == tag:
                yield child
            else:
                self.error(where, f"unexpected element <{child.tag}>")

    def number(self, where, name, text, convert, low, high):
        if text is None:
            return None  # already reported as missing
        try:
            value = convert(text)
        except ValueError:
            self.error(where, f"{name} '{text}' is not a valid number")
            return None
        if not low <= value <= high:
            self.error(where, f"{name} {text} is outside {low}..{high}")
            return None
        return value

    def choice(self, where, name, value, allowed):
        if value is not None and value not in allowed:
            self.error(where, f"{name} '{value}' must be one of: {', '.join(allowed)}")

    def id(self, where, name, value):
        if value is not None and not (_ID.fullmatch(value) and int(value, 16)):
            self.error(where, f"{name} '{value}' is not the {name} of your organisation "
                              "as in your GSDML file, such as 0x1234")
            return None
        return value and int(value, 16)

    def match(self, where, name, value, pattern):
        if value is not None and not pattern.fullmatch(value):
            self.error(where, f"{name} '{value}' is not valid")

    def field(self, element, where, module, used):
        raw = module.type == "RAW"
        required = ["path", "offset"] + (["as"] if raw else [])
        encoding = element.get("as") if raw else MODULE_ENCODING[module.type]
        if encoding == "BOOL":
            required.append("bit")
        a = self.attributes(element, where, required)
        path = a.get("path")
        where = f"{where} field '{path}'"
        self.match(where, "path", path, _PATH)
        if encoding is None:
            return None
        self.choice(where, "as", encoding, ENCODINGS)
        if encoding not in ENCODINGS:
            return None
        offset = self.number(where, "offset", a.get("offset"), int, 0, module.size - 1)
        bit = self.number(where, "bit", a.get("bit"), int, 0, 7) if encoding == "BOOL" else 0
        if offset is None or bit is None:
            return None
        loc = Location(encoding, offset, bit)
        if offset + loc.size > module.size:
            self.error(where, f"{loc.size} bytes at offset {offset} do not fit in "
                              f"{module.name} ({module.size} bytes)")
            return None
        if not raw and offset % loc.size:
            self.error(where, f"offset {offset} is not a multiple of {loc.size}")
        if encoding == "BOOL":
            bits = {offset * 8 + bit}
        else:
            bits = set(range(offset * 8, (offset + loc.size) * 8))
        if bits & used:
            self.error(where, f"overlaps another field at offset {offset}")
        used |= bits
        return Field(path, loc)

    def mapping(self, element, where, number, module, used):
        is_in = module.direction == "IN"
        a = self.attributes(
            element, where,
            ("topic", "type", "timeout_ms" if is_in else "on_disconnect"),
            ("qos",) if is_in else ("qos", "rate_hz"))
        topic = a.get("topic")
        where = f"{where} topic '{topic}'"
        self.match(where, "topic", topic, _TOPIC)
        self.match(where, "type", a.get("type"), _TYPE)
        qos = a.get("qos", "sensor" if is_in else "default")
        self.choice(where, "qos", qos, QOS)
        timeout = rate = None
        on_disconnect = "stop"
        if is_in:
            if a.get("timeout_ms") != "none":
                ms = self.number(where, "timeout_ms", a.get("timeout_ms"), int, 1, 3600000)
                timeout = ms / 1000 if ms else None
        else:
            on_disconnect = a.get("on_disconnect", "stop")
            self.choice(where, "on_disconnect", on_disconnect, ON_DISCONNECT)
            if "rate_hz" in a:
                rate = self.number(where, "rate_hz", a["rate_hz"], float, 0.01, 1000)
            elif on_disconnect == "hold":
                self.error(where, "on_disconnect='hold' needs rate_hz")
        fields = [self.field(f, where, module, used)
                  for f in self.children(element, where, "field")]
        if not fields:
            self.error(where, "has no <field>")
        return Mapping(number, module, topic, a.get("type"), tuple(f for f in fields if f),
                       qos, timeout, on_disconnect, rate)

    def slot(self, element, seen):
        where = f"slot {element.get('number')}"
        a = self.attributes(element, where, ("number", "module"))
        number = self.number(where, "number", a.get("number"), int, STATUS_SLOT + 1, MAX_SLOT)
        if number is not None:
            if number in seen:
                self.error(where, "is defined twice")
            seen.add(number)
        module = MODULES.get(a.get("module"))
        if module is None and "module" in a:
            self.error(where, f"unknown module '{a['module']}'")
        if module is None or number is None:
            return None
        used = set()
        maps = [self.mapping(m, where, number, module, used)
                for m in self.children(element, where, "map")]
        if not maps:
            self.error(where, "has no <map>")
        return Slot(number, module, tuple(maps))

    def topics(self, slots):
        types, ins, outs = {}, set(), set()
        for m in (m for s in slots for m in s.mappings if m.topic):
            where = f"slot {m.slot} topic '{m.topic}'"
            topic = m.topic.lstrip("/")  # "odom" and "/odom" are normally the same
            if types.setdefault(topic, m.type) != m.type:
                self.error(where, f"is also used with type {types[topic]}")
            if m.module.direction == "IN":
                ins.add(topic)
            elif topic in outs:
                self.error(where, "is published from more than one <map>")
            else:
                outs.add(topic)
        for topic in sorted(ins & outs):
            self.error(f"topic '{topic}'", "is used in both directions")

    def config(self, root):
        where = "profinet"
        a = self.attributes(root, where, ("vendor_id", "device_id", "station_name", "interface"),
                            ("cycle_ms",))
        ids = [self.id(where, name, a.get(name)) for name in ("vendor_id", "device_id")]
        name = a.get("station_name")
        if name is not None and not (len(name) <= 240 and all(
                _NAME_LABEL.fullmatch(part) for part in name.split("."))):
            self.error(where, f"station_name '{name}' may only use a-z, 0-9, '-' and '.'")
        self.match(where, "interface", a.get("interface"), re.compile(r"[\w.@:-]{1,15}"))
        cycle = self.number(where, "cycle_ms", a.get("cycle_ms", "10"), int, 1, 100) or 10
        seen = set()
        slots = [self.slot(s, seen) for s in self.children(root, where, "slot")]
        slots = tuple(sorted((s for s in slots if s), key=lambda s: s.number))
        if not slots and not self.errors:
            self.error(where, "has no <slot>")
        self.topics(slots)
        for m in (m for s in slots for m in s.mappings):
            if m.rate_hz and m.rate_hz * cycle > 1000:
                self.error(f"slot {m.slot} topic '{m.topic}'",
                           f"rate_hz {m.rate_hz:g} is faster than cycle_ms {cycle} allows")
        config = Config(*ids, name, a.get("interface"), cycle / 1000, slots)
        for direction in ("IN", "OUT"):
            if config.io_bytes(direction) > MAX_IO_BYTES:
                self.error(where, f"{direction} needs {config.io_bytes(direction)} bytes, "
                                  f"the limit is {MAX_IO_BYTES}")
        if self.errors:
            raise ConfigError("\n".join(self.errors))
        return config


def parse(text):
    try:
        root = ET.fromstring(text)
    except ET.ParseError as e:
        raise ConfigError(f"not valid XML: {e}") from None
    if root.tag != "profinet":
        raise ConfigError(f"the root element must be <profinet>, not <{root.tag}>")
    return _Reader().config(root)


def load(path):
    with open(path, "rb") as f:
        return parse(f.read())
