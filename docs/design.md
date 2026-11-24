# Design

- `profinet_device` does the cyclic exchange in a real-time thread and knows nothing
  about ROS. A crash of the node does not disturb the PLC connection.
- `profinet_bridge` is the ROS 2 node. It reads the XML file, converts between topics
  and module data, and knows nothing about PROFINET.
- They are connected by a Unix socket. The messages are described in
  `profinet_bridge/profinet_bridge/protocol.py` and implemented in
  `profinet_device/src/protocol.c`. Each side notices at once if the other one ends.
- p-net must not be used by two threads at once. `pnal_lock()` (see
  [third_party](../profinet_device/third_party/README.md)) is held for each tick and
  each received frame.
- The mapping ID is a checksum of slots, modules, topics, types, fields and offsets.
  Timeouts and rates do not change it.
