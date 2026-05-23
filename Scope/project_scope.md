# Project Scope — Klippylink

## Communication Chain

The Linux host talks to the ESP32-C3 dongle over the C3's USB Serial/JTAG console, which appears as a serial device on Linux such as `/dev/ttyACM*`. The C3 then acts as the BLE side that talks wirelessly to the ESP32-S3. The S3 can expose a GATT server, and the C3 can act as the GATT client/central that sends commands to it and receives status/data back. ESP-IDF supports BLE on both ESP32-C3 and ESP32-S3, and Espressif documents both GATT server and BLE connection/data-exchange flows. (Espressif Systems)

The full chain is:

```
Linux app ⇄ USB serial on ESP32-C3 ⇄ BLE ⇄ ESP32-S3
```

## Protocol Model

The Linux host sends high-level commands over serial to the C3 (e.g. "set motor speed to 30" or "read sensor packet"), and the C3 firmware translates those into BLE GATT writes/reads/notifications toward the S3. Data coming back from the S3 is then forwarded by the C3 back to Linux over the same USB serial link.

> **Important:** The C3 will **not** automatically behave like a standard plug-and-play BLE USB dongle for Linux (comparable to a generic HCI Bluetooth adapter). The USB Serial/JTAG console is a serial console/debug channel; it does not expose a host Bluetooth controller interface to Linux. Unless a dedicated USB protocol and matching Linux-side program are built, Linux will just see a serial device, not a native Bluetooth adapter. (Espressif Systems)

## Software Flow

1. Linux sends commands over serial.
2. The C3 parses them.
3. The C3 manages the BLE connection to the S3.
4. The S3 executes the requested action.
5. The S3 returns data by GATT read response or notifications/indications.
6. The C3 forwards that data to Linux.

## BLE Role Split

The S3 should be the **BLE peripheral / GATT server**, and the C3 dongle should be the **BLE central / GATT client**, because the dongle is the "master bridge" attached to the PC and is the one initiating control. ESP-IDF's BLE docs and APIs align naturally with that split. (Espressif Systems)

## Performance Considerations

BLE is fine for:

- Commands
- Telemetry
- Configuration
- Moderate-rate status updates

BLE is a **poor fit** for:

- Very high throughput
- Hard real-time control loops
- Extremely low-latency streaming

If the Linux host needs information for supervisory decisions, UI, or moderate-speed feedback, BLE is fine. If tight real-time closed-loop control at high update rates is required, BLE will become the bottleneck.

## Implementation Split

| Component | Responsibility |
|---|---|
| **Linux** | Small Python program talking to `/dev/ttyACM0` |
| **ESP32-C3** | Command parser + BLE central/client logic |
| **ESP32-S3** | GATT server exposing command and telemetry characteristics |

## Tech Stack

- **Embedded Linux:** Plateforme.io
- **Linux application:** Python
- **MCU firmware:** ESP-IDF (BLE on both C3 and S3)
