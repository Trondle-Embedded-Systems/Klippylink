import logging
from typing import Callable, Optional

import serial

from shared.protocol.klip_protocol import KlipCommand, KlipPacket

logger = logging.getLogger(__name__)

DEFAULT_SERIAL_PORT = "/dev/ttyACM0"
DEFAULT_BAUDRATE = 115200

FRAMED_HEADER_SIZE = 5  # node_id(1) + magic(2) + cmd(1) + len(1)

# Callback type: (node_id: int, packet: KlipPacket) -> None
PacketCallback = Callable[[int, KlipPacket], None]


class SerialTransport:
    def __init__(
        self,
        port: str = DEFAULT_SERIAL_PORT,
        baudrate: int = DEFAULT_BAUDRATE,
        on_packet: Optional[PacketCallback] = None,
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

    def send_packet(self, node_id: int, packet: KlipPacket) -> None:
        """Send a framed packet: [node_id u8][klip_packet bytes]."""
        if not self._serial or not self._serial.is_open:
            raise RuntimeError("Serial port not open")
        frame = bytes([node_id & 0xFF]) + packet.encode()
        self._serial.write(frame)
        logger.debug("TX: node=%d cmd=0x%02X len=%d",
                     node_id, packet.command, len(packet.payload))

    def poll(self) -> None:
        if not self._serial or not self._serial.is_open:
            return
        data = self._serial.read(self._serial.in_waiting or 1)
        if data:
            self._buffer.extend(data)
            self._process_buffer()

    def _process_buffer(self) -> None:
        """Parse [node_id u8][magic(2) cmd(1) len(1) payload(N)] frames."""
        while len(self._buffer) >= FRAMED_HEADER_SIZE:
            node_id = self._buffer[0]
            magic = int.from_bytes(self._buffer[1:3], "little")
            if magic != 0x4B4C:
                # Not a valid frame start — discard the node_id byte and retry.
                self._buffer.pop(0)
                continue
            length = self._buffer[4]
            frame_len = FRAMED_HEADER_SIZE + length
            if len(self._buffer) < frame_len:
                break
            klip_frame = bytes(self._buffer[1:frame_len])
            del self._buffer[:frame_len]
            packet = KlipPacket.decode(klip_frame)
            if packet and self.on_packet:
                logger.debug("RX: node=%d cmd=0x%02X len=%d",
                             node_id, packet.command, len(packet.payload))
                self.on_packet(node_id, packet)
