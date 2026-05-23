from enum import IntEnum, auto
from dataclasses import dataclass
from struct import pack, unpack
from typing import Optional

MAGIC = 0x4B4C
HEADER_SIZE = 4
MAX_PAYLOAD_SIZE = 244


class KlipCommand(IntEnum):
    PING = 0x01
    PONG = 0x02
    SET_MOTOR_SPEED = 0x10
    READ_SENSOR = 0x20
    SENSOR_RESPONSE = 0x21
    SET_CONFIG = 0x30
    GET_STATUS = 0x40
    STATUS_RESPONSE = 0x41
    ERROR = 0xFF


@dataclass
class KlipPacket:
    command: KlipCommand
    payload: bytes = b""

    def encode(self) -> bytes:
        return pack("<HBB", MAGIC, self.command, len(self.payload)) + self.payload

    @staticmethod
    def decode(data: bytes) -> Optional["KlipPacket"]:
        if len(data) < HEADER_SIZE:
            return None
        magic, cmd, length = unpack("<HBB", data[:HEADER_SIZE])
        if magic != MAGIC:
            return None
        payload = data[HEADER_SIZE : HEADER_SIZE + length]
        if len(payload) < length:
            return None
        try:
            command = KlipCommand(cmd)
        except ValueError:
            command = cmd
        return KlipPacket(command=command, payload=payload)
