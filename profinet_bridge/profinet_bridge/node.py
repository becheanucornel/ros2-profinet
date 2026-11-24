# SPDX-License-Identifier: Apache-2.0
"""The ROS 2 node: subscribes and publishes as config_profinet.xml says."""
import functools
import signal
import socket
import sys
import time

import rclpy
from rclpy.executors import ExternalShutdownException
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy
from rosidl_runtime_py.utilities import get_message

from . import protocol
from .config import ConfigError, load
from .field_path import check_field
from .runtime import InputSlot, OutputTopic

DAEMON_TIMEOUT_S = 0.5
RECONNECT_S = 1.0
_STATES = {protocol.NOT_IN_PLC: "is not in the PLC project",
           protocol.MATCH: "matches the PLC project",
           protocol.MISMATCH: "has a different module in the PLC project"}


def _qos(name):
    if name == "sensor":  # best effort also receives from reliable publishers
        return QoSProfile(depth=1, reliability=ReliabilityPolicy.BEST_EFFORT)
    if name == "latched":
        return QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)
    return QoSProfile(depth=1)


class ProfinetBridge(Node):
    def __init__(self):
        super().__init__("profinet_bridge")
        path = self.declare_parameter("config", "").value
        self._socket_path = self.declare_parameter(
            "socket", "/run/profinet_device/profinet_device.sock").value
        if not path:
            raise ConfigError("parameter 'config' must be the path of config_profinet.xml")
        self._config = load(path)
        types = self._load_types()

        self._inputs, self._outputs, self._subs = [], [], []
        for s in self._config.slots:
            if s.module.direction == "IN":
                slot = InputSlot(s)
                self._inputs.append(slot)
                for m in s.mappings:
                    self._subs.append(self.create_subscription(
                        types[m.type], m.topic,
                        functools.partial(self._on_message, slot, m), _qos(m.qos)))
            else:
                for m in s.mappings:
                    pub = self.create_publisher(types[m.type], m.topic, _qos(m.qos))
                    self._outputs.append((OutputTopic(m, types[m.type]), pub))

        self._sock = None
        self._next_connect = 0.0
        self._received = None       # last Outputs
        self._received_at = 0.0
        self._heartbeat = 0
        self._reported = {}
        self._plc = None
        self.create_timer(self._config.cycle_s, self._cycle)
        self.get_logger().info(
            f"{len(self._config.slots)} slots, mapping ID 0x{self._config.mapping_id:08X}")

    def _load_types(self):
        types, errors = {}, []
        for s in self._config.slots:
            for m in s.mappings:
                if m.type not in types:
                    try:
                        types[m.type] = get_message(m.type)
                    except (ImportError, AttributeError, ValueError) as e:
                        types[m.type] = None
                        errors.append(f"message type '{m.type}' is not available: {e}")
                if types[m.type] is None:
                    continue
                for f in m.fields:
                    problem = check_field(types[m.type](), f.path,
                                          f.location.encoding, s.module.direction)
                    if problem:
                        errors.append(f"slot {s.number} topic '{m.topic}': {problem}")
        if errors:
            raise ConfigError("\n".join(errors))
        return types

    def _on_message(self, slot, mapping, msg):
        problem = slot.on_message(mapping, msg, time.monotonic())
        if problem:
            self.get_logger().warning(problem, throttle_duration_sec=5.0)

    def _cycle(self):
        now = time.monotonic()
        self._connect(now)
        self._receive(now)
        if self._sock and now - self._received_at > DAEMON_TIMEOUT_S:
            self._disconnect("profinet_device stopped answering")
        self._publish(now)
        self._send(now)

    def _connect(self, now):
        if self._sock or now < self._next_connect:
            return
        self._next_connect = now + RECONNECT_S
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        try:
            sock.setblocking(False)
            sock.connect(self._socket_path)
            sock.send(protocol.encode_hello(self._config))
        except OSError as e:
            sock.close()
            self.get_logger().warning(f"cannot reach profinet_device at {self._socket_path}: {e}",
                                      throttle_duration_sec=10.0)
            return
        self._sock = sock
        self._received_at = now
        self._reported = {}
        self._plc = None

    def _disconnect(self, reason):
        self.get_logger().error(reason)
        self._sock.close()
        self._sock = None
        self._received = None

    def _receive(self, now):
        while self._sock:
            try:
                packet = self._sock.recv(protocol.MAX_PACKET)
            except BlockingIOError:
                return
            except OSError as e:
                self._disconnect(f"lost profinet_device: {e}")
                return
            if not packet:
                self._disconnect("profinet_device closed the connection")
                return
            try:
                message = protocol.decode(packet)
            except protocol.ProtocolError as e:
                self._disconnect(str(e))
                return
            if isinstance(message, str):
                self._disconnect(f"profinet_device refused this node: {message}")
                return
            self._received, self._received_at = message, now
            self._report(message)

    def _report(self, message):
        plc = (message.plc_connected, message.plc_running)
        if plc != self._plc:
            self._plc = plc
            self.get_logger().info("PLC " + ("disconnected" if not plc[0] else
                                             "connected, RUN" if plc[1] else "connected, STOP"))
        if not message.plc_connected:
            self._reported = {}
            return
        for slot, out in message.slots.items():
            if self._reported.get(slot) != out.state:
                self._reported[slot] = out.state
                log = self.get_logger()
                text = f"slot {slot} {_STATES.get(out.state, out.state)}"
                if out.state == protocol.MATCH:
                    log.info(text)
                else:
                    log.warning(text)

    def _publish(self, now):
        message = self._received
        for output, publisher in self._outputs:
            data = None
            if message and message.plc_connected and message.plc_running:
                slot = message.slots.get(output.mapping.slot)
                if (slot and slot.valid and slot.state == protocol.MATCH
                        and len(slot.data) == output.mapping.module.size):
                    data = slot.data
            msg = output.step(now, data)
            if output.problem:
                self.get_logger().warning(output.problem, throttle_duration_sec=5.0)
            if msg is not None:
                publisher.publish(msg)

    def _send(self, now):
        if not self._received:
            return  # not accepted yet
        self._heartbeat += 1
        packet = protocol.encode_inputs(self._heartbeat, [s.image(now) for s in self._inputs])
        try:
            self._sock.send(packet)
        except BlockingIOError:
            pass  # profinet_device is busy; the next cycle sends newer data
        except OSError as e:
            self._disconnect(f"lost profinet_device: {e}")


def main():
    rclpy.init()
    try:
        node = ProfinetBridge()
    except (ConfigError, OSError) as e:
        print(f"profinet_bridge: configuration error\n{e}", file=sys.stderr)
        rclpy.shutdown()
        return 1
    try:
        rclpy.spin(node)
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        # ros2 launch passes on a Ctrl+C that the node has already received.
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        node.destroy_node()
        rclpy.try_shutdown()
    return 0
