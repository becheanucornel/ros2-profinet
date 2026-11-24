# SPDX-License-Identifier: Apache-2.0
"""Data handling between topics and slots. Contains no ROS code."""
import math

from . import codec
from .field_path import field_limits, get_field, set_field


class InputSlot:
    """Collects topic values into the bytes of one IN slot."""

    def __init__(self, slot):
        self.number = slot.number
        self._data = bytearray(slot.module.size)
        self._updated = {m: None for m in slot.mappings}  # time of the last good message

    def on_message(self, mapping, msg, now):
        """Store a message. Returns a problem, or None. A bad message invalidates the slot."""
        data = bytearray(self._data)
        try:
            for f in mapping.fields:
                codec.write(data, f.location, get_field(msg, f.path))
        except (codec.CodecError, AttributeError, IndexError, TypeError) as e:
            self._updated[mapping] = None
            return f"slot {self.number} topic '{mapping.topic}': {e}"
        self._data = data
        self._updated[mapping] = now
        return None

    def image(self, now):
        valid = all(t is not None and (m.timeout_s is None or now - t <= m.timeout_s)
                    for m, t in self._updated.items())
        return self.number, valid, bytes(self._data)


class OutputTopic:
    """Decides when and what to publish for one OUT mapping."""

    def __init__(self, mapping, new_message):
        self.mapping = mapping
        self._new_message = new_message
        self._limits = [field_limits(new_message(), f.path) for f in mapping.fields]
        self.problem = None      # why the last data was not usable
        self._was_valid = False
        self._values = None      # last published values, None before the first
        self._next = 0.0

    def _decode(self, data):
        values = [codec.read(data, f.location) for f in self.mapping.fields]
        for f, value, limits in zip(self.mapping.fields, values, self._limits):
            if not math.isfinite(value) or limits and not limits[0] <= value <= limits[1]:
                self.problem = f"topic '{self.mapping.topic}': {value} does not fit in '{f.path}'"
                return None
        return values

    def step(self, now, data):
        """data: the slot's bytes, or None while they are not valid. Returns a message or None.

        A value that the topic cannot hold makes the data invalid, too.
        """
        m = self.mapping
        changed = False
        self.problem = None
        values = None if data is None else self._decode(data)
        valid = values is not None
        if valid:
            changed = not self._was_valid or values != self._values
            self._values = values
        elif self._was_valid and m.on_disconnect == "zero":
            self._values = [0] * len(m.fields)
            changed = True
        self._was_valid = valid

        if self._values is None:
            return None
        if m.rate_hz is None:
            due = changed
        elif not valid and m.on_disconnect == "stop":
            due = False
        else:
            # Losing the PLC must take effect at once, not at the next period.
            due = now >= self._next or (changed and not valid)
        if not due:
            return None
        if m.rate_hz is not None:
            period = 1 / m.rate_hz
            on_time = 0 <= now - self._next < period
            self._next = (self._next if on_time else now) + period
        msg = self._new_message()
        for f, value in zip(m.fields, self._values):
            set_field(msg, f.path, bool(value) if f.location.encoding == "BOOL" else value)
        return msg
