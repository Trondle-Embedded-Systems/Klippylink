import argparse
import asyncio
import logging
import sys
import threading
import time

import uvicorn

from serial_transport import SerialTransport, DEFAULT_SERIAL_PORT
from device_manager import DeviceManager
from moonraker_client import MoonrakerClient
from heartbeat_monitor import HeartbeatMonitor
from ota_handler import OtaHandler
import api_server
from shared.protocol.klip_protocol import KlipCommand

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger(__name__)


def main() -> None:
    parser = argparse.ArgumentParser(description="Klippylink Linux host")
    parser.add_argument("--port",      default=DEFAULT_SERIAL_PORT)
    parser.add_argument("--baudrate",  type=int, default=115200)
    parser.add_argument("--api-port",  type=int, default=8080)
    parser.add_argument("--moonraker", default="http://localhost:7125")
    args = parser.parse_args()

    device_mgr = DeviceManager()
    moonraker  = MoonrakerClient(args.moonraker)
    transport  = SerialTransport(args.port, args.baudrate)
    ota        = OtaHandler(transport)

    # Dispatch incoming packets to DeviceManager and OtaHandler
    def on_packet(node_id: int, pkt) -> None:
        device_mgr.on_packet(node_id, pkt)
        if pkt.command == KlipCommand.OTA_STATUS:
            ota.on_ota_status(node_id, pkt)

    transport.on_packet = on_packet
    transport.open()
    logger.info("Serial transport open on %s", args.port)

    # Serial polling runs in a background thread (synchronous pyserial)
    def poll_loop():
        while True:
            try:
                transport.poll()
            except Exception as exc:
                logger.error("Serial poll error: %s", exc)
                time.sleep(1)

    poll_thread = threading.Thread(target=poll_loop, daemon=True)
    poll_thread.start()

    # Wire up API server
    api_server.init(transport, device_mgr, ota)

    # Start heartbeat monitor in the asyncio event loop
    async def serve():
        hb = HeartbeatMonitor(transport, device_mgr, moonraker)
        asyncio.create_task(hb.run())
        config = uvicorn.Config(api_server.app,
                                host="0.0.0.0", port=args.api_port,
                                log_level="info")
        server = uvicorn.Server(config)
        await server.serve()

    try:
        asyncio.run(serve())
    except KeyboardInterrupt:
        logger.info("Shutting down")
    finally:
        transport.close()


if __name__ == "__main__":
    main()
