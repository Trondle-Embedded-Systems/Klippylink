from enum import IntEnum
from dataclasses import dataclass, field
from struct import pack, unpack, pack_into
from typing import Optional

MAGIC = 0x4B4C
HEADER_SIZE = 4  # magic(2) + command(1) + length(1)
MAX_PAYLOAD_SIZE = 244


class KlipCommand(IntEnum):
    PING               = 0x01
    PONG               = 0x02
    SET_MOTOR_SPEED    = 0x10
    READ_SENSOR        = 0x20
    SENSOR_RESPONSE    = 0x21
    SET_CONFIG         = 0x30
    GET_STATUS         = 0x40
    STATUS_RESPONSE    = 0x41
    NEOPIXEL_SET       = 0x50
    NEOPIXEL_SET_ALL   = 0x51
    NEOPIXEL_SET_RANGE = 0x52
    ENDSTOP_QUERY      = 0x60
    ENDSTOP_STATE      = 0x61
    ENDSTOP_SUBSCRIBE  = 0x62
    DEVICE_INFO_REQ    = 0x70
    DEVICE_INFO_RESP   = 0x71
    OTA_BEGIN          = 0x80
    OTA_DATA           = 0x81
    OTA_END            = 0x82
    OTA_STATUS         = 0x83
    ERROR              = 0xFF


OTA_OK          = 0x00
OTA_ERROR       = 0x01
OTA_IN_PROGRESS = 0x02


@dataclass
class KlipPacket:
    command: KlipCommand
    payload: bytes = field(default=b"")

    def encode(self) -> bytes:
        return pack("<HBB", MAGIC, int(self.command), len(self.payload)) + self.payload

    @staticmethod
    def decode(data: bytes) -> Optional["KlipPacket"]:
        if len(data) < HEADER_SIZE:
            return None
        magic, cmd, length = unpack("<HBB", data[:HEADER_SIZE])
        if magic != MAGIC:
            return None
        payload = data[HEADER_SIZE: HEADER_SIZE + length]
        if len(payload) < length:
            return None
        try:
            command = KlipCommand(cmd)
        except ValueError:
            command = cmd  # type: ignore[assignment]
        return KlipPacket(command=command, payload=payload)


# ── Convenience builders ──────────────────────────────────────────────────────

def make_neopixel_set(led_idx: int, r: int, g: int, b: int) -> KlipPacket:
    return KlipPacket(KlipCommand.NEOPIXEL_SET, pack("BBBB", led_idx, r, g, b))


def make_neopixel_set_all(r: int, g: int, b: int) -> KlipPacket:
    return KlipPacket(KlipCommand.NEOPIXEL_SET_ALL, pack("BBB", r, g, b))


def make_neopixel_set_range(start: int, count: int, r: int, g: int, b: int) -> KlipPacket:
    return KlipPacket(KlipCommand.NEOPIXEL_SET_RANGE, pack("BBBBB", start, count, r, g, b))


def make_endstop_query(endstop_idx: int) -> KlipPacket:
    return KlipPacket(KlipCommand.ENDSTOP_QUERY, pack("B", endstop_idx))


def make_endstop_subscribe(endstop_idx: int, enable: bool) -> KlipPacket:
    return KlipPacket(KlipCommand.ENDSTOP_SUBSCRIBE, pack("BB", endstop_idx, int(enable)))


def make_device_info_req() -> KlipPacket:
    return KlipPacket(KlipCommand.DEVICE_INFO_REQ)


def make_ota_begin(size: int, crc32: int) -> KlipPacket:
    return KlipPacket(KlipCommand.OTA_BEGIN, pack("<II", size, crc32))


def make_ota_data(chunk: bytes) -> KlipPacket:
    return KlipPacket(KlipCommand.OTA_DATA, chunk)


def make_ota_end() -> KlipPacket:
    return KlipPacket(KlipCommand.OTA_END)


def decode_endstop_state(payload: bytes):
    """Returns (endstop_idx, triggered) or None."""
    if len(payload) < 2:
        return None
    idx, triggered = unpack("BB", payload[:2])
    return idx, bool(triggered)


def decode_ota_status(payload: bytes):
    """Returns status byte or None."""
    if not payload:
        return None
    return payload[0]
