import logging
from typing import Any

import httpx

logger = logging.getLogger(__name__)

DEFAULT_MOONRAKER_URL = "http://localhost:7125"


class MoonrakerClient:
    def __init__(self, base_url: str = DEFAULT_MOONRAKER_URL) -> None:
        self._base = base_url.rstrip("/")
        self._client = httpx.AsyncClient(timeout=5.0)

    async def call_macro(self, name: str) -> None:
        url = f"{self._base}/printer/gcode/script"
        try:
            resp = await self._client.post(url, json={"script": name})
            resp.raise_for_status()
        except Exception as exc:
            logger.error("Moonraker macro '%s' failed: %s", name, exc)

    async def get_heater_state(self, heater_name: str) -> dict[str, Any]:
        url = f"{self._base}/printer/objects/query"
        try:
            resp = await self._client.get(url,
                                          params={"heater_generic": heater_name})
            resp.raise_for_status()
            return resp.json().get("result", {}).get("status", {})
        except Exception as exc:
            logger.warning("get_heater_state failed: %s", exc)
            return {}

    async def close(self) -> None:
        await self._client.aclose()
