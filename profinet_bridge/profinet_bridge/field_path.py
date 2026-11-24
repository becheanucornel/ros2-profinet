# SPDX-License-Identifier: Apache-2.0
"""Read and write message fields by path, such as 'pose.position.x' or 'ranges[3]'."""
import re

_FLOAT32_MAX = (2 - 2 ** -23) * 2.0 ** 127
_LIMITS = {"float": (-_FLOAT32_MAX, _FLOAT32_MAX)}
for _bits in (8, 16, 32, 64):
    _LIMITS[f"int{_bits}"] = (-2 ** (_bits - 1), 2 ** (_bits - 1) - 1)
    _LIMITS[f"uint{_bits}"] = (0, 2 ** _bits - 1)

_TOKEN = re.compile(r"([A-Za-z_]\w*)|\[(\d+)\]")


def _tokens(path):
    return [name if name else int(index) for name, index in _TOKEN.findall(path)]


def _step(obj, token):
    return obj[token] if isinstance(token, int) else getattr(obj, token)


def get_field(msg, path):
    for token in _tokens(path):
        msg = _step(msg, token)
    return msg.item() if hasattr(msg, "item") else msg  # numpy scalar


def set_field(msg, path, value):
    *parents, last = _tokens(path)
    for token in parents:
        msg = _step(msg, token)
    if isinstance(_step(msg, last), float):
        value = float(value)
    if isinstance(last, int):
        msg[last] = value
    else:
        setattr(msg, last, value)


def field_limits(msg, path):
    """Return the smallest and largest value of a field, or None if it has no limits.

    ROS does not check them: 300 in an int8 field would be published as 44.
    """
    kind = ""
    for token in _tokens(path):
        if isinstance(token, str) and hasattr(msg, "get_fields_and_field_types"):
            kind = msg.get_fields_and_field_types().get(token, "")
        msg = _step(msg, token)
    return _LIMITS.get(kind.split("[")[0])


def check_field(msg, path, encoding, direction):
    """Return what is wrong with mapping this field, or None. msg is a default message."""
    obj = msg
    for token in _tokens(path):
        try:
            obj = _step(obj, token)
        except (AttributeError, TypeError):
            return f"field '{path}' does not exist"
        except IndexError:
            if direction == "IN":
                return None  # variable-length array: checked for each message
            return f"field '{path}' is in a variable-length array, which cannot be written"
    if getattr(obj, "shape", None) == ():
        obj = obj.item()
    is_bool = isinstance(obj, bool)
    if not isinstance(obj, (bool, int, float)):
        return f"field '{path}' is not a number or bool"
    if (encoding == "BOOL") != is_bool:
        return f"field '{path}': a BOOL can only be mapped to a BOOL"
    if direction == "OUT" and isinstance(obj, int) and encoding in ("F32", "F64"):
        return f"field '{path}' is an integer and cannot take {encoding} data"
    return None
