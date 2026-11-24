# SPDX-License-Identifier: Apache-2.0
"""Check config_profinet.xml without ROS, and show what the PLC programmer needs."""
import argparse
import sys

from .config import ConfigError, load


def report(config):
    lines = [f"Device         vendor ID 0x{config.vendor_id:04X}, "
             f"device ID 0x{config.device_id:04X}",
             f"Mapping ID     16#{config.mapping_id:08X}",
             f"Frame bytes    IN {config.io_bytes('IN')}, OUT {config.io_bytes('OUT')}", "",
             "Slot  Module           Offset  Type  Topic and field",
             "   1  NODE_STATUS_IN   fixed"]
    for s in config.slots:
        for m in s.mappings:
            for f in m.fields:
                loc = f.location
                offset = f"{loc.offset}.{loc.bit}" if loc.encoding == "BOOL" else loc.offset
                lines.append(f"{s.number:4}  {s.module.name:15}  {offset!s:>6}  "
                             f"{loc.encoding:4}  {m.topic} {f.path}")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(prog="profinet_check", description=__doc__)
    parser.add_argument("config")
    parser.add_argument("--service-env", action="store_true",
                        help="print the settings file for the profinet_device service instead")
    args = parser.parse_args(argv)
    try:
        config = load(args.config)
    except (ConfigError, OSError) as e:
        print(e, file=sys.stderr)
        return 1
    if args.service_env:
        print(f"PROFINET_VENDOR_ID=0x{config.vendor_id:04X}\n"
              f"PROFINET_DEVICE_ID=0x{config.device_id:04X}\n"
              f"PROFINET_INTERFACE={config.interface}\n"
              f"PROFINET_STATION_NAME={config.station_name}")
    else:
        print(report(config))
    return 0


if __name__ == "__main__":
    sys.exit(main())
