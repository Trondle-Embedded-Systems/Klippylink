"""Shared Klippylink protocol definitions."""

from .klip_protocol import KlipCommand, KlipPacket, MAGIC, HEADER_SIZE, MAX_PAYLOAD_SIZE

__all__ = [
    "KlipCommand",
    "KlipPacket",
    "MAGIC",
    "HEADER_SIZE",
    "MAX_PAYLOAD_SIZE",
]
