import asyncio
import logging
from pathlib import Path
from struct import pack
from typing import Any

from fastapi import FastAPI, HTTPException, UploadFile
from fastapi.responses import StreamingResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from shared.protocol.klip_protocol import (
    KlipCommand, KlipPacket,
    make_wifi_enable, make_heater_set,
    make_radio_set, make_radio_get,
)
from device_manager import DeviceManager, LedZone, RadioSettings
from serial_transport import SerialTransport
from ota_handler import OtaHandler

logger = logging.getLogger(__name__)

app = FastAPI(title="Klippylink Host API")

# Injected at startup — see main.py
_transport: SerialTransport | None = None
_device_mgr: DeviceManager | None = None
_ota_handler: OtaHandler | None = None


def init(transport: SerialTransport,
         device_mgr: DeviceManager,
         ota_handler: OtaHandler) -> None:
    global _transport, _device_mgr, _ota_handler
    _transport  = transport
    _device_mgr = device_mgr
    _ota_handler = ota_handler

    static_dir = Path(__file__).parent / "static"
    if static_dir.is_dir():
        app.mount("/", StaticFiles(directory=str(static_dir), html=True),
                  name="static")


def _get_node(name: str):
    node = _device_mgr.get_node_by_name(name)
    if not node or not node.connected:
        raise HTTPException(404, f"Node '{name}' not found or not connected")
    return node


# ── Info ───────────────────────────────────────────────────────────────────────

@app.get("/api/info")
def get_info() -> dict[str, Any]:
    return {
        "firmware": "klippylink-host",
        "nodes_connected": sum(1 for n in _device_mgr.list_nodes() if n.connected),
    }


# ── Nodes ──────────────────────────────────────────────────────────────────────

@app.get("/api/nodes")
def list_nodes() -> list[dict]:
    return [
        {
            "node_id":    n.node_id,
            "name":       n.name,
            "connected":  n.connected,
            "last_seen":  n.last_seen,
            "capabilities": n.capabilities,
        }
        for n in _device_mgr.list_nodes()
    ]


@app.get("/api/nodes/{name}/status")
def node_status(name: str) -> dict:
    node = _get_node(name)
    return {
        "node_id":     node.node_id,
        "name":        node.name,
        "connected":   node.connected,
        "capabilities": node.capabilities,
    }


# ── LED zones ──────────────────────────────────────────────────────────────────

class LedZoneBody(BaseModel):
    zone_id:    int
    strip:      int = 0
    start:      int
    count:      int
    effect:     int
    color:      list[int]   # [r, g, b]
    color2:     list[int]   # [r, g, b]
    brightness: int = 255
    speed:      int = 128
    name:       str = ""


@app.get("/api/nodes/{name}/leds/zones")
def list_led_zones(name: str) -> list[dict]:
    node = _get_node(name)
    return [
        {
            "zone_id":    z.zone_id,
            "strip":      z.strip,
            "start":      z.start,
            "count":      z.count,
            "effect":     z.effect,
            "color":      z.color,
            "color2":     z.color2,
            "brightness": z.brightness,
            "speed":      z.speed,
            "name":       z.name,
        }
        for z in node.led_zones.values()
    ]


@app.post("/api/nodes/{name}/leds/zones")
def set_led_zone(name: str, body: LedZoneBody) -> dict:
    node = _get_node(name)
    payload = pack("BBBBBBBBBBBB",
                   body.zone_id, body.strip, body.start, body.count, body.effect,
                   body.color[0], body.color[1], body.color[2],
                   body.color2[0], body.color2[1], body.color2[2],
                   body.brightness) + bytes([body.speed])
    _transport.send_packet(node.node_id,
                           KlipPacket(KlipCommand.LED_ZONE_SET, payload))
    _device_mgr.set_zone(node.node_id, LedZone(
        zone_id=body.zone_id, strip=body.strip, start=body.start,
        count=body.count, effect=body.effect,
        color=body.color, color2=body.color2,
        brightness=body.brightness, speed=body.speed,
        name=body.name,
    ))
    return {"ok": True}


@app.delete("/api/nodes/{name}/leds/zones/{zone_id}")
def clear_led_zone(name: str, zone_id: int) -> dict:
    node = _get_node(name)
    _transport.send_packet(node.node_id,
                           KlipPacket(KlipCommand.LED_ZONE_CLR,
                                      bytes([zone_id])))
    _device_mgr.clear_zone(node.node_id, zone_id)
    return {"ok": True}


# ── Radio settings ─────────────────────────────────────────────────────────────

class RadioBody(BaseModel):
    power:             int  = 4
    channel:           int  = 80
    conn_interval_ms:  int  = 20
    telemetry_enabled: bool = True


@app.get("/api/nodes/{name}/radio")
def get_radio(name: str) -> dict:
    node = _get_node(name)
    r = node.radio
    return {
        "power":             r.power,
        "channel":           r.channel,
        "conn_interval_ms":  r.conn_interval_ms,
        "telemetry_enabled": r.telemetry_enabled,
    }


@app.put("/api/nodes/{name}/radio")
def set_radio(name: str, body: RadioBody) -> dict:
    node = _get_node(name)
    node.radio = RadioSettings(
        power=body.power,
        channel=body.channel,
        conn_interval_ms=body.conn_interval_ms,
        telemetry_enabled=body.telemetry_enabled,
    )
    pkt = make_radio_set(body.power, body.channel,
                         body.conn_interval_ms, body.telemetry_enabled)
    _transport.send_packet(node.node_id, pkt)
    return {"ok": True}


# ── WiFi enable ────────────────────────────────────────────────────────────────

class WifiBody(BaseModel):
    ssid:     str
    password: str


@app.post("/api/nodes/{name}/wifi/enable")
def enable_wifi(name: str, body: WifiBody) -> dict:
    node = _get_node(name)
    pkt = make_wifi_enable(body.ssid, body.password)
    _transport.send_packet(node.node_id, pkt)
    return {"ok": True, "message": "WIFI_ENABLE sent — watch for WIFI_STATUS event"}


# ── Heater ─────────────────────────────────────────────────────────────────────

class HeaterBody(BaseModel):
    target_temp: float
    kp: float = 1.0
    ki: float = 0.1
    kd: float = 0.01


@app.post("/api/nodes/{name}/heater/set")
def set_heater(name: str, body: HeaterBody) -> dict:
    node = _get_node(name)
    pkt = make_heater_set(body.target_temp, body.kp, body.ki, body.kd)
    _transport.send_packet(node.node_id, pkt)
    return {"ok": True}


# ── OTA firmware upload ────────────────────────────────────────────────────────

@app.post("/api/nodes/{name}/firmware")
async def upload_firmware(name: str, firmware: UploadFile):
    node = _get_node(name)
    data = await firmware.read()
    if not data:
        raise HTTPException(400, "Empty firmware file")

    progress_queue: asyncio.Queue[tuple[int, int] | None] = asyncio.Queue()

    def progress_cb(sent: int, total: int) -> None:
        progress_queue.put_nowait((sent, total))

    async def ota_task():
        ok = await _ota_handler.push_firmware(node.node_id, data, progress_cb)
        progress_queue.put_nowait(None)
        return ok

    asyncio.create_task(ota_task())

    async def event_stream():
        while True:
            item = await progress_queue.get()
            if item is None:
                yield "data: done\n\n"
                break
            sent, total = item
            pct = int(sent / total * 100)
            yield f"data: {pct}%  {sent}/{total}\n\n"

    return StreamingResponse(event_stream(), media_type="text/event-stream")
