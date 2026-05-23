# Klippylink

Serial-to-BLE bridge: Linux host ⇄ ESP32-C3 dongle ⇄ ESP32-S3 peripheral.

```
Linux app ⇄ USB serial (ESP32-C3) ⇄ BLE ⇄ ESP32-S3
```

## Architecture

```
Klippylink/
├── shared/              # Protocol definitions (shared between all targets)
│   └── protocol/
│       ├── klip_protocol.h   # C header: packet format, commands, constants
│       ├── klip_protocol.py  # Python: packet encode/decode
│       └── __init__.py
├── linux-host/          # Python application running on the Linux host
│   ├── src/
│   │   ├── main.py            # CLI entry point
│   │   └── serial_transport.py
│   ├── tests/
│   └── pyproject.toml
├── esp32-c3/            # ESP-IDF project — BLE central / serial bridge dongle
│   ├── main/
│   │   ├── main.c             # UART init + main loop
│   │   ├── command_parser.c/h # Serial → packet parser
│   │   └── ble_central.c/h    # BLE GATT client
│   └── CMakeLists.txt
├── esp32-s3/            # ESP-IDF project — BLE peripheral / GATT server
│   ├── main/
│   │   ├── main.c             # Application entry
│   │   └── gatt_server.c/h    # GATT server with command & telemetry chars
│   └── CMakeLists.txt
├── Scope/
│   └── project_scope.md
└── .gitignore
```

## Tech Stack

| Component | Technology |
|---|---|
| Linux host | Python + pyserial |
| ESP32-C3 firmware | ESP-IDF (BLE central) |
| ESP32-S3 firmware | ESP-IDF (GATT server) |
| Serial protocol | Custom binary (see `shared/protocol/`) |

## Building

### ESP32-C3 / ESP32-S3

```bash
cd esp32-c3   # or esp32-s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

### Linux Host

```bash
cd linux-host
pip install -e .
python -m src.main --port /dev/ttyACM0
```
