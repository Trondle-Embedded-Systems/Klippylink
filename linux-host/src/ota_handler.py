import asyncio
import logging
import zlib
from struct import pack
from typing import Callable, Optional

from shared.protocol.klip_protocol import (
    KlipCommand, KlipPacket, OTA_OK, OTA_ERROR, OTA_IN_PROGRESS,
)
from serial_transport import SerialTransport

logger = logging.getLogger(__name__)

CHUNK_SIZE = 244


def _chunks(data: bytes, size: int):
    for i in range(0, len(data), size):
        yield data[i:i + size]


class OtaHandler:
    def __init__(self, transport: SerialTransport) -> None:
        self._transport = transport
        self._pending: dict[int, asyncio.Future] = {}

    def on_ota_status(self, node_id: int, pkt: KlipPacket) -> None:
        """Call from the packet dispatcher when KLIPCMD_OTA_STATUS arrives."""
        future = self._pending.get(node_id)
        if future and not future.done():
            status = pkt.payload[0] if pkt.payload else OTA_ERROR
            future.set_result(status)

    async def push_firmware(
        self,
        node_id: int,
        firmware: bytes,
        progress_cb: Optional[Callable[[int, int], None]] = None,
    ) -> bool:
        """
        Upload firmware to a node via the serial bridge (→ router BLE OTA).
        Returns True on success.
        """
        crc = zlib.crc32(firmware) & 0xFFFFFFFF
        total = len(firmware)

        loop = asyncio.get_event_loop()
        self._pending[node_id] = loop.create_future()

        begin_pkt = KlipPacket(KlipCommand.OTA_BEGIN,
                               pack("<II", total, crc))
        self._transport.send_packet(node_id, begin_pkt)

        try:
            status = await asyncio.wait_for(self._pending[node_id], timeout=10.0)
        except asyncio.TimeoutError:
            logger.error("OTA begin timeout for node %d", node_id)
            return False
        finally:
            self._pending.pop(node_id, None)

        if status != OTA_IN_PROGRESS:
            logger.error("OTA begin rejected by node %d (status=%d)", node_id, status)
            return False

        sent = 0
        for chunk in _chunks(firmware, CHUNK_SIZE):
            self._transport.send_packet(node_id,
                                        KlipPacket(KlipCommand.OTA_DATA, chunk))
            sent += len(chunk)
            if progress_cb:
                progress_cb(sent, total)
            await asyncio.sleep(0)  # yield to event loop

        self._pending[node_id] = loop.create_future()
        self._transport.send_packet(node_id, KlipPacket(KlipCommand.OTA_END))

        try:
            status = await asyncio.wait_for(self._pending[node_id], timeout=30.0)
        except asyncio.TimeoutError:
            logger.error("OTA end timeout for node %d", node_id)
            return False
        finally:
            self._pending.pop(node_id, None)

        if status == OTA_OK:
            logger.info("OTA complete for node %d (%d bytes)", node_id, total)
            return True

        logger.error("OTA failed for node %d (status=%d)", node_id, status)
        return False
