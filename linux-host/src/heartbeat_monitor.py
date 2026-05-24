import asyncio
import logging
import time

from shared.protocol.klip_protocol import KlipCommand, KlipPacket
from device_manager import DeviceManager
from moonraker_client import MoonrakerClient
from serial_transport import SerialTransport

logger = logging.getLogger(__name__)

HEARTBEAT_INTERVAL_S = 0.5
HEARTBEAT_TIMEOUT_S  = 2.0


class HeartbeatMonitor:
    """
    Sends HEARTBEAT to nodes that have an active heater.
    If a node doesn't respond within HEARTBEAT_TIMEOUT_S, triggers
    the KLIPPYLINK_HEATER_EMERGENCY Moonraker macro.
    """

    def __init__(self,
                 transport: SerialTransport,
                 device_mgr: DeviceManager,
                 moonraker: MoonrakerClient) -> None:
        self._transport  = transport
        self._device_mgr = device_mgr
        self._moonraker  = moonraker
        # node_id → last heartbeat response time
        self._last_hb: dict[int, float] = {}
        # node_id → True if heater is active
        self._heater_active: dict[int, bool] = {}
        # node_id → last known heater config (target, kp, ki, kd)
        self._heater_cfg: dict[int, tuple] = {}

    def set_heater_active(self, node_id: int, active: bool,
                          cfg: tuple | None = None) -> None:
        self._heater_active[node_id] = active
        if cfg:
            self._heater_cfg[node_id] = cfg
        if active:
            self._last_hb[node_id] = time.monotonic()

    def on_heartbeat_response(self, node_id: int) -> None:
        self._last_hb[node_id] = time.monotonic()

    async def run(self) -> None:
        hb_pkt = KlipPacket(KlipCommand.HEARTBEAT)
        while True:
            await asyncio.sleep(HEARTBEAT_INTERVAL_S)
            now = time.monotonic()
            for node_id, active in list(self._heater_active.items()):
                if not active:
                    continue
                node = self._device_mgr.get_node_by_id(node_id)
                if not node or not node.connected:
                    continue
                try:
                    self._transport.send_packet(node_id, hb_pkt)
                except Exception as exc:
                    logger.warning("Heartbeat TX failed for node %d: %s",
                                   node_id, exc)

                last = self._last_hb.get(node_id, now)
                if now - last > HEARTBEAT_TIMEOUT_S:
                    logger.error(
                        "Heartbeat timeout for node %d — triggering emergency",
                        node_id)
                    await self._moonraker.call_macro(
                        "KLIPPYLINK_HEATER_EMERGENCY")
                    self._heater_active[node_id] = False
