import argparse
import logging
import sys
import time

from serial_transport import SerialTransport, DEFAULT_SERIAL_PORT
from shared.protocol.klip_protocol import KlipCommand, KlipPacket

logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger(__name__)


def on_packet_received(packet: KlipPacket) -> None:
    logger.info("Received packet: cmd=%s payload=%s", packet.command.name, packet.payload.hex())


def main() -> None:
    parser = argparse.ArgumentParser(description="Klippylink Linux host")
    parser.add_argument("--port", default=DEFAULT_SERIAL_PORT, help="Serial port")
    parser.add_argument("--baudrate", type=int, default=115200, help="Baud rate")
    args = parser.parse_args()

    transport = SerialTransport(
        port=args.port,
        baudrate=args.baudrate,
        on_packet=on_packet_received,
    )

    try:
        transport.open()
        logger.info("Klippylink host running on %s", args.port)
        while True:
            transport.poll()
            time.sleep(0.01)
    except KeyboardInterrupt:
        logger.info("Shutting down")
    finally:
        transport.close()


if __name__ == "__main__":
    main()
