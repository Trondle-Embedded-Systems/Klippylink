# Klippylink

Serial-to-BLE bridge: Linux host ⇄ ESP32 router dongle ⇄ ESP32 node peripheral.

```
Linux app ⇄ USB serial (router) ⇄ BLE ⇄ node
```

Any common ESP32 variant (C3, S3, C5, …) can run either role. Role is determined by
which firmware you flash, not which chip you use.

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
├── router/              # ESP-IDF project — BLE central / WiFi bridge (any ESP32)
│   ├── main/
│   │   ├── main.c
│   │   ├── ble_central.c/h
│   │   ├── wifi_manager.c/h
│   │   └── http_server.c/h
│   ├── sdkconfig.defaults               # Role defaults (chip-agnostic)
│   ├── sdkconfig.defaults.esp32c3       # C3-specific overrides
│   ├── sdkconfig.defaults.esp32s3       # S3-specific overrides
│   ├── sdkconfig.defaults.esp32c5       # C5-specific overrides
│   └── CMakeLists.txt
├── node/                # ESP-IDF project — BLE peripheral / GATT server (any ESP32)
│   ├── main/
│   │   ├── main.c
│   │   ├── gatt_server.c/h
│   │   ├── neopixel.c/h
│   │   └── endstop.c/h
│   ├── sdkconfig.defaults               # Role defaults (chip-agnostic)
│   ├── sdkconfig.defaults.esp32c3       # C3-specific overrides
│   ├── sdkconfig.defaults.esp32s3       # S3-specific overrides
│   ├── sdkconfig.defaults.esp32c5       # C5-specific overrides
│   └── CMakeLists.txt
├── web-flasher/
├── klipper-extension/
├── Scope/
└── .gitignore
```

## Tech Stack

| Component | Technology |
|---|---|
| Linux host | Python + pyserial |
| Router firmware | ESP-IDF (BLE central + WiFi) |
| Node firmware | ESP-IDF (GATT server) |
| Serial protocol | Custom binary (see `shared/protocol/`) |

## Building

### Router or Node (any supported ESP32 variant)

```bash
cd router   # or: cd node
idf.py set-target esp32c3   # or esp32s3, esp32c5, …
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

`idf.py set-target` automatically merges the matching `sdkconfig.defaults.<target>` file
(CPU frequency, etc.) on top of the role defaults.

### Linux Host

```bash
cd linux-host
pip install -e .
python -m src.main --port /dev/ttyACM0
```
