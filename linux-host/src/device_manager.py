import json
import logging
import time
from dataclasses import dataclass, field
from typing import Optional

from shared.protocol.klip_protocol import KlipCommand, KlipPacket

logger = logging.getLogger(__name__)


@dataclass
class NodeInfo:
    node_id: int
    name: str
    connected: bool = True
    capabilities: dict = field(default_factory=dict)
    last_seen: float = field(default_factory=time.monotonic)


class DeviceManager:
    """Maintains an in-memory registry of connected nodes."""

    def __init__(self) -> None:
        self._nodes: dict[int, NodeInfo] = {}

    def on_packet(self, node_id: int, pkt: KlipPacket) -> None:
        """Feed a received packet into the manager for bookkeeping."""
        if pkt.command == KlipCommand.PING:
            if node_id not in self._nodes:
                self._nodes[node_id] = NodeInfo(node_id=node_id,
                                                name=f"node_{node_id}")
            else:
                self._nodes[node_id].connected = True
            logger.info("Node %d connected", node_id)

        elif pkt.command == KlipCommand.ERROR:
            if node_id in self._nodes:
                self._nodes[node_id].connected = False
            logger.info("Node %d disconnected", node_id)

        elif pkt.command == KlipCommand.DEVICE_INFO_RESP:
            try:
                info = json.loads(pkt.payload.decode())
                if node_id in self._nodes:
                    node = self._nodes[node_id]
                    node.name = info.get("name", node.name)
                    node.capabilities = info
                    node.last_seen = time.monotonic()
            except Exception as exc:
                logger.warning("Failed to parse device info for node %d: %s",
                               node_id, exc)

        elif node_id in self._nodes:
            self._nodes[node_id].last_seen = time.monotonic()

    def list_nodes(self) -> list[NodeInfo]:
        return list(self._nodes.values())

    def get_node_by_id(self, node_id: int) -> Optional[NodeInfo]:
        return self._nodes.get(node_id)

    def get_node_by_name(self, name: str) -> Optional[NodeInfo]:
        for node in self._nodes.values():
            if node.name == name:
                return node
        return None
