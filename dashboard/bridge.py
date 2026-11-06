
"""VibeGuard USB-serial to WebSocket bridge.

Reads telemetry from an ESP32 and broadcasts normalized JSON to browser clients.
Accepted ESP32 formats:
  TEL:{"vector_rms":0.041,"state":"NORMAL",...}
  {"vector_rms":0.041,"state":"NORMAL",...}
  >VRMS:0.041,AlarmThresh:0.35,State:0

WebSocket endpoint: ws://127.0.0.1:8765
"""

from __future__ import annotations

import argparse
import asyncio
import json
import logging
import re
import signal
import threading
import time
from dataclasses import dataclass
from typing import Any

import serial
from serial.tools import list_ports
from websockets.asyncio.server import ServerConnection, serve

LOG = logging.getLogger("vibeguard-bridge")
LEGACY_RE = re.compile(
    r"^>VRMS:(?P<rms>[-+]?\d*\.?\d+),"
    r"AlarmThresh:(?P<threshold>[-+]?\d*\.?\d+),"
    r"State:(?P<state>[01])$"
)


@dataclass(slots=True)
class Config:
    port: str | None
    baud: int
    host: str
    ws_port: int
    reconnect_delay: float


def choose_serial_port(requested: str | None) -> str:
    if requested:
        return requested

    ports = list(list_ports.comports())
    if not ports:
        raise RuntimeError("No serial devices found. Connect the ESP32 and try again.")

    preferred_words = ("cp210", "silicon labs", "ch340", "ch341", "usb serial", "uart")
    preferred = [
        p for p in ports
        if any(word in f"{p.description} {p.manufacturer or ''}".lower() for word in preferred_words)
    ]
    selected = (preferred or ports)[0]
    LOG.info("Auto-selected %s (%s)", selected.device, selected.description)
    return selected.device


def normalize_packet(packet: dict[str, Any]) -> dict[str, Any]:
    aliases = {
        "rms": "vector_rms",
        "vrms": "vector_rms",
        "alarm_threshold": "alarm_thr",
        "warning_threshold": "warning_thr",
    }
    data = {aliases.get(key, key): value for key, value in packet.items()}

    state = str(data.get("state", "UNKNOWN")).upper()
    if state in {"0", "FALSE"}:
        state = "NORMAL"
    elif state in {"1", "TRUE"}:
        state = "ALARM"
    data["state"] = state

    if "led" not in data:
        data["led"] = {
            "NORMAL": "GREEN",
            "WARNING": "BLUE",
            "ALARM": "RED",
            "FAULT": "BLUE",
        }.get(state, "OFF")

    data.setdefault("sensor_ok", state != "FAULT")
    data.setdefault("fault", "NONE" if data["sensor_ok"] else "SENSOR_FAULT")
    data.setdefault("motor_pwm", 0)
    data.setdefault("motor_percent", round(float(data["motor_pwm"]) * 100 / 255))
    data.setdefault("device_ts_ms", data.pop("ts", None))
    data["bridge_ts_ms"] = int(time.time() * 1000)
    data["type"] = "telemetry"
    return data


def parse_serial_line(line: str) -> dict[str, Any] | None:
    text = line.strip()
    if not text:
        return None

    payload = text[4:].strip() if text.startswith("TEL:") else text
    if payload.startswith("{") and payload.endswith("}"):
        try:
            decoded = json.loads(payload)
        except json.JSONDecodeError as exc:
            LOG.warning("Invalid JSON ignored: %s (%s)", payload, exc)
            return None
        if not isinstance(decoded, dict):
            return None
        return normalize_packet(decoded)

    match = LEGACY_RE.fullmatch(text)
    if match:
        alarm = match.group("state") == "1"
        return normalize_packet({
            "vector_rms": float(match.group("rms")),
            "alarm_thr": float(match.group("threshold")),
            "state": "ALARM" if alarm else "NORMAL",
            "sensor_ok": True,
            "led": "RED" if alarm else "GREEN",
        })

    return None


class Bridge:
    def __init__(self, config: Config) -> None:
        self.config = config
        self.clients: set[ServerConnection] = set()
        self.serial_link: serial.Serial | None = None
        self.serial_lock = threading.Lock()
        self.stop_event = threading.Event()
        self.loop: asyncio.AbstractEventLoop | None = None
        self.queue: asyncio.Queue[dict[str, Any]] = asyncio.Queue(maxsize=256)
        self.latest: dict[str, Any] | None = None
        self.port_name: str | None = None

    def queue_from_thread(self, packet: dict[str, Any]) -> None:
        def put() -> None:
            if self.queue.full():
                try:
                    self.queue.get_nowait()
                except asyncio.QueueEmpty:
                    pass
            self.queue.put_nowait(packet)

        if self.loop and not self.loop.is_closed():
            self.loop.call_soon_threadsafe(put)

    def serial_worker(self) -> None:
        while not self.stop_event.is_set():
            try:
                self.port_name = choose_serial_port(self.config.port)
                LOG.info("Opening %s at %d baud", self.port_name, self.config.baud)
                with serial.Serial(
                    self.port_name,
                    self.config.baud,
                    timeout=1,
                    write_timeout=1,
                ) as link:
                    with self.serial_lock:
                        self.serial_link = link
                    time.sleep(1.5)
                    link.reset_input_buffer()
                    LOG.info("ESP32 serial connection ready")

                    while not self.stop_event.is_set():
                        raw = link.readline()
                        if not raw:
                            continue
                        line = raw.decode("utf-8", errors="replace").strip()
                        packet = parse_serial_line(line)
                        if packet:
                            self.queue_from_thread(packet)
                        elif line:
                            LOG.debug("ESP32: %s", line)
            except (serial.SerialException, OSError, RuntimeError) as exc:
                LOG.warning("Serial unavailable: %s", exc)
            finally:
                with self.serial_lock:
                    self.serial_link = None
                self.port_name = None

            if not self.stop_event.wait(self.config.reconnect_delay):
                LOG.info("Retrying serial connection")

    def write_serial_command(self, command: str) -> bool:
        command = command.strip()
        if not command or len(command) > 64:
            return False
        with self.serial_lock:
            if not self.serial_link or not self.serial_link.is_open:
                return False
            try:
                self.serial_link.write((command + "\n").encode("utf-8"))
                self.serial_link.flush()
                return True
            except serial.SerialException as exc:
                LOG.warning("Command send failed: %s", exc)
                return False

    async def websocket_handler(self, websocket: ServerConnection) -> None:
        self.clients.add(websocket)
        LOG.info("Dashboard connected (%d client%s)", len(self.clients), "s" if len(self.clients) != 1 else "")
        try:
            await websocket.send(json.dumps({
                "type": "bridge_status",
                "serial_connected": bool(self.serial_link and self.serial_link.is_open),
                "port": self.port_name,
                "baud": self.config.baud,
            }))
            if self.latest:
                await websocket.send(json.dumps(self.latest, separators=(",", ":")))

            async for message in websocket:
                try:
                    request = json.loads(message)
                except json.JSONDecodeError:
                    await websocket.send(json.dumps({"type": "error", "message": "Invalid JSON command"}))
                    continue

                command = str(request.get("command", "")).strip()
                sent = self.write_serial_command(command)
                await websocket.send(json.dumps({
                    "type": "command_ack",
                    "command": command,
                    "sent": sent,
                }))
        finally:
            self.clients.discard(websocket)
            LOG.info("Dashboard disconnected (%d remaining)", len(self.clients))

    async def broadcaster(self) -> None:
        while True:
            packet = await self.queue.get()
            self.latest = packet
            message = json.dumps(packet, separators=(",", ":"), allow_nan=False)
            if not self.clients:
                continue
            results = await asyncio.gather(
                *(client.send(message) for client in tuple(self.clients)),
                return_exceptions=True,
            )
            for client, result in zip(tuple(self.clients), results):
                if isinstance(result, Exception):
                    self.clients.discard(client)

    async def status_broadcaster(self) -> None:
        while True:
            await asyncio.sleep(2)
            if not self.clients:
                continue
            status = json.dumps({
                "type": "bridge_status",
                "serial_connected": bool(self.serial_link and self.serial_link.is_open),
                "port": self.port_name,
                "baud": self.config.baud,
                "bridge_ts_ms": int(time.time() * 1000),
            })
            await asyncio.gather(
                *(client.send(status) for client in tuple(self.clients)),
                return_exceptions=True,
            )

    async def run(self) -> None:
        self.loop = asyncio.get_running_loop()
        worker = threading.Thread(target=self.serial_worker, name="serial-reader", daemon=True)
        worker.start()

        async with serve(
            self.websocket_handler,
            self.config.host,
            self.config.ws_port,
            ping_interval=20,
            ping_timeout=20,
            max_size=64 * 1024,
        ):
            LOG.info("WebSocket ready at ws://%s:%d", self.config.host, self.config.ws_port)
            broadcast_task = asyncio.create_task(self.broadcaster())
            status_task = asyncio.create_task(self.status_broadcaster())
            try:
                await asyncio.Future()
            finally:
                self.stop_event.set()
                broadcast_task.cancel()
                status_task.cancel()
                await asyncio.gather(broadcast_task, status_task, return_exceptions=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="VibeGuard ESP32 serial-to-WebSocket bridge")
    parser.add_argument("--port", help="Serial port, e.g. COM5 or /dev/ttyUSB0; auto-detect if omitted")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate (default: 115200)")
    parser.add_argument("--host", default="127.0.0.1", help="WebSocket bind host (default: 127.0.0.1)")
    parser.add_argument("--ws-port", type=int, default=8764, help="WebSocket port (default: 8765)")
    parser.add_argument("--reconnect-delay", type=float, default=2.0, help="Serial reconnect delay in seconds")
    parser.add_argument("--verbose", action="store_true", help="Show non-telemetry serial/debug lines")
    return parser.parse_args()


async def main() -> None:
    args = parse_args()
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s | %(levelname)-7s | %(message)s",
        datefmt="%H:%M:%S",
    )
    config = Config(args.port, args.baud, args.host, args.ws_port, args.reconnect_delay)
    bridge = Bridge(config)

    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, lambda: [task.cancel() for task in asyncio.all_tasks(loop) if task is not asyncio.current_task(loop)])
        except (NotImplementedError, RuntimeError):
            pass

    try:
        await bridge.run()
    except asyncio.CancelledError:
        bridge.stop_event.set()
        LOG.info("Bridge stopped")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
