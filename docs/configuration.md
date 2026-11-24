# config_profinet.xml

[The example](../profinet_bridge/config/config_profinet.xml) shows every setting, and
[`config_profinet.xsd`](../profinet_bridge/config/config_profinet.xsd) gives completion
in editors. To change a mapping, edit the file and restart the node. TIA Portal is
needed only if modules or slots change. After a change of the IDs, `station_name` or
`interface`, also [update the settings of the service](installation.md#4-write-config_profinetxml).

`<profinet>`

| Attribute | Meaning |
|---|---|
| `vendor_id`, `device_id` | [IDs of your organisation and the device](../gsdml/README.md#1-your-ids), such as `0x1234`, as in your GSDML file |
| `station_name` | PROFINET name, with `a-z`, `0-9`, `-` and `.` |
| `interface` | Network port of the PROFINET network |
| `cycle_ms` | How often the node exchanges data with the service, 1 to 100, default 10 |

`<slot number="2..32" module="...">` has the same module as the slot in the PLC
project. IN modules carry data to the PLC, OUT modules carry data from the PLC.

`<map>`

| Attribute | For | Meaning |
|---|---|---|
| `topic`, `type` | both | Topic, and message type such as `std_msgs/msg/Bool` |
| `timeout_ms` | IN, required | The data is bad if no message arrives for this long. `none` for topics that are published only once |
| `on_disconnect` | OUT, required | What to do when the PLC data is not usable: `stop` publishing, publish `zero`, or `hold` the last values (needs `rate_hz`) |
| `rate_hz` | OUT | Publish continuously. Without it, only when a value changes. At most `1000 / cycle_ms` |
| `qos` | both | IN: `sensor` (default, works with every publisher), `default`, or `latched` to get the last message of a latched topic. OUT: `default` or `latched` |

Nothing is published to an OUT topic before the first usable data from the PLC.

**A topic without `rate_hz` is published once per change. A node that starts later
does not get it, unless both use `latched`.** But a latched topic also gives a late
node the last value after the PLC was lost, if `on_disconnect` is `stop`.

`<field>`

| Attribute | Meaning |
|---|---|
| `path` | Field of the message, such as `pose.position.x` or `ranges[3]` |
| `offset` | Bytes from the start of the module |
| `bit` | 0 to 7, for BOOL |
| `as` | For RAW modules only: `BOOL`, `I8`, `U8`, `I16`, `U16`, `I32`, `U32`, `I64`, `U64`, `F32` or `F64` |

Floats are rounded if the PLC data is an integer. An integer field cannot take
REAL data. Variable-length arrays can be sent to the PLC, not received.
