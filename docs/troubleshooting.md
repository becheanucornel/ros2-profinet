# Behaviour and troubleshooting

## What happens when

| If | Then |
|---|---|
| A topic is silent for `timeout_ms` | The data of its slot is bad (IOPS) until a message arrives. All topics of a slot must be good. |
| A value is NaN, infinite or too large for the PLC data | The same, and a warning in the ROS log |
| The PLC is lost or in STOP, or marks a slot as bad | `on_disconnect` |
| The PLC sends NaN, infinity, or a value that is too large for the topic | `on_disconnect` for that topic, and a warning in the ROS log |
| The node stops | All its data is bad at once; the PLC connection stays up |
| The node hangs | The same after 0.5 s |
| The PROFINET service stops | `on_disconnect` for all topics; the PLC sees a lost device |
| A slot has other modules in the XML and the PLC | The PLC gets "wrong module" for it, and its data is bad; the others run |
| A node starts, or restarts with a changed XML, so that this changes while the PLC is connected | The device closes the connection once, so that the PLC reconnects and sees it |
| A slot is only in the XML | A bit in the Node status module and a warning in the ROS log |
| A slot is only in the PLC | Accepted, with bad data |
| The PLC vanishes while it connects | It can connect again after at most 20 s |
| The cable is unplugged and plugged in again | `on_disconnect`, then the PLC reconnects. Its first attempt may fail, because the port is not ready yet. |
| A second node starts | Refused; the first one keeps running |

## When something does not work

| Symptom | Look at |
|---|---|
| Node: `cannot reach profinet_device ... Permission denied` | Is the user in the group `profinet`, and logged in again? |
| Node: `cannot reach profinet_device ... refused` or `No such file` | `systemctl status profinet_device` and `journalctl -u profinet_device` |
| `p-net could not be started` | Does the port exist, and is the service running as root? It retries every 2 s. |
| TIA Portal does not import the GSDML file | Are its [placeholders](../gsdml/README.md#1-your-ids) replaced? |
| Service: `the vendor ID and device ID of your organisation are needed` | Are they in `/etc/default/profinet_device`? See [Installation](installation.md#4-write-config_profinetxml). |
| PLC: wrong device, or no connection although the device is found | Are the IDs the same in the GSDML file and in `profinet_check`? |
| TIA Portal does not find the device | Is it the right port, and is its link up? |
| PLC: wrong module | `profinet_check` shows the slots and modules of the XML |
| PLC: device lost again and again | Use a longer update time or a larger watchdog factor |
| `slot ... has a different module` in the ROS log | Same as wrong module |
