import asyncio
import logging
from typing import Callable, Optional

import serial
import serial.aio

from shared.protocol.klip_protocol import KlipCommand, KlipPacket

logger = logging.getLogger(__name__)

DEFAULT_SERIAL_PORT = "/dev/ttyACM0"
DEFAULT_BAUDRATE = 115200


class SerialTransport:
    def __init__(
        self,
        port: str = DEFAULT_SERIAL_PORT,
        baudrate: int = DEFAULT_BAUDRATE,
        on_packet: Optional[Callable[[KlipPacket], None]] = None,
    ):
        self.port = port
        self.baudrate = baudrate
        self.on_packet = on_packet
        self._serial: Optional[serial.Serial] = None
        self._buffer = bytearray()

    def open(self) -> None:
        self._serial = serial.Serial(self.port, self.baudrate, timeout=1)
        logger.info("Opened serial port %s at %d baud", self.port, self.baudrate)

    def close(self) -> None:
        if self._serial and self._serial.is_open:
            self._serial.close()
            logger.info("Closed serial port %s", self.port)

    def send_packet(self, packet: KlipPacket) -> None:
        if not self._serial or not self._serial.is_open:
            raise RuntimeError("Serial port not open")
        data = packet.encode()
        self._serial.write(data)
        logger.debug("TX: cmd=0x%02X len=%d", packet.command, len(packet.payload))

    def poll(self) -> None:
        if not self._serial or not self._serial.is_open:
            return
        data = self._serial.read(self._serial.in_waiting or 1)
        if data:
            self._buffer.extend(data)
            self._process_buffer()

    def _process_buffer(self) -> None:
        while len(self._buffer) >= 4:
            magic = int.from_bytes(self._buffer[0:2], "little")
            if magic != 0x4B4C:
                self._buffer.pop(0)
                continue
            length = self._buffer[3]
            frame_len = 4 + length
            if len(self._buffer) < frame_len:
                break
            frame = bytes(self._buffer[:frame_len])
            del self._buffer[:frame_len]
            packet = KlipPacket.decode(frame)
            if packet and self.on_packet:
                logger.debug(
                    "RX: cmd=0x%02X len=%d", packet.command, len(packet.payload)
                )
                self.on_packet(packet)
