import sys
import os
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "shared"))

from shared.protocol.klip_protocol import KlipCommand, KlipPacket, MAGIC, HEADER_SIZE


class TestKlipProtocol(unittest.TestCase):
    def test_encode_decode_roundtrip(self):
        packet = KlipPacket(command=KlipCommand.PING, payload=b"\x01\x02\x03")
        encoded = packet.encode()
        decoded = KlipPacket.decode(encoded)
        self.assertEqual(decoded.command, KlipCommand.PING)
        self.assertEqual(decoded.payload, b"\x01\x02\x03")

    def test_decode_bad_magic(self):
        data = b"\x00\x00\x01\x00"
        self.assertIsNone(KlipPacket.decode(data))

    def test_decode_truncated(self):
        data = b"\x4C\x4B\x01\x05\x01"
        self.assertIsNone(KlipPacket.decode(data))

    def test_empty_payload(self):
        packet = KlipPacket(command=KlipCommand.GET_STATUS)
        encoded = packet.encode()
        self.assertEqual(len(encoded), HEADER_SIZE)
        decoded = KlipPacket.decode(encoded)
        self.assertEqual(decoded.payload, b"")


if __name__ == "__main__":
    unittest.main()
