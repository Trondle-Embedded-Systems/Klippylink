"""
KlippyLink Klipper extra — bridges Klipper to KlippyLink BLE nodes via the
router's HTTP + WebSocket API.

Config sections:

  [klippylink router_name]
  host: 192.168.1.42          # router IP
  port: 8765                  # default

  [klippylink_neopixel my_leds]
  router: router_name
  node:   toolhead_node       # node device_name from its config.json
  strip:  status_leds         # strip name from node config.json
  count:  8

  [klippylink_endstop x_min]
  router: router_name
  node:   toolhead_node
  endstop: x_min              # endstop name from node config.json

GCode commands produced:

  SET_KLIPPYLINK_LED STRIP=my_leds RED=0 GREEN=0.5 BLUE=0 [INDEX=3]
  QUERY_KLIPPYLINK_ENDSTOP ENDSTOP=x_min
"""

import urllib.request
import urllib.error
import json
import threading
import logging

logger = logging.getLogger(__name__)

POLL_INTERVAL_S = 0.1       # endstop polling interval


# ── Router ────────────────────────────────────────────────────────────────────

class KlippylinkRouter:
    def __init__(self, config):
        self.printer  = config.get_printer()
        self.name     = config.get_name().split()[-1]
        self.host     = config.get('host')
        self.port     = config.getint('port', 8765)
        self._base    = f"http://{self.host}:{self.port}"
        self._lock    = threading.Lock()
        self._ws      = None

        self.printer.register_event_handler("klippy:connect",
                                             self._handle_connect)
        self.printer.register_event_handler("klippy:disconnect",
                                             self._handle_disconnect)

    def _handle_connect(self):
        try:
            info = self._get("/api/info")
            logger.info("KlippyLink router '%s' connected: %s", self.name, info)
        except Exception as e:
            raise self.printer.config_error(
                f"Cannot reach KlippyLink router '{self.name}' "
                f"at {self._base}: {e}"
            )
        self._start_ws()

    def _handle_disconnect(self):
        self._stop_ws()

    # ── HTTP helpers ──────────────────────────────────────────────────────────

    def _get(self, path):
        url = self._base + path
        req = urllib.request.Request(url)
        with urllib.request.urlopen(req, timeout=5) as resp:
            return json.loads(resp.read())

    def _post(self, path, body):
        url  = self._base + path
        data = json.dumps(body).encode()
        req  = urllib.request.Request(
            url, data=data,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=5) as resp:
            return json.loads(resp.read())

    # ── Public API used by sub-objects ────────────────────────────────────────

    def set_neopixel(self, node, strip, r, g, b, index=None):
        body = {"r": int(r * 255), "g": int(g * 255), "b": int(b * 255)}
        if index is not None:
            body["index"] = index
        try:
            self._post(f"/api/nodes/{node}/neopixels/{strip}/set", body)
        except Exception as e:
            logger.warning("set_neopixel failed: %s", e)

    def query_endstop(self, node, endstop):
        try:
            data = self._get(f"/api/nodes/{node}/endstops/{endstop}")
            return data.get("triggered", False)
        except Exception as e:
            logger.warning("query_endstop failed: %s", e)
            return None

    # ── WebSocket event stream (background thread) ────────────────────────────

    def _start_ws(self):
        try:
            import websocket  # pip install websocket-client
            ws_url = f"ws://{self.host}:{self.port}/ws/events"
            self._ws = websocket.WebSocketApp(
                ws_url,
                on_message=self._on_ws_message,
                on_error=lambda ws, e: logger.warning("WS error: %s", e),
            )
            self._ws_thread = threading.Thread(
                target=self._ws.run_forever, daemon=True)
            self._ws_thread.start()
            logger.info("KlippyLink WS event stream connected")
        except ImportError:
            logger.warning("websocket-client not installed; "
                           "endstop events will be polled only")
        except Exception as e:
            logger.warning("WS connect failed: %s", e)

    def _stop_ws(self):
        if self._ws:
            self._ws.close()
            self._ws = None

    def _on_ws_message(self, ws, raw):
        try:
            ev = json.loads(raw)
            evt_type = ev.get("event")
            if evt_type == "endstop":
                self.printer.send_event(
                    "klippylink:endstop_change",
                    ev.get("node"), ev.get("idx"), ev.get("triggered"),
                )
        except Exception as e:
            logger.debug("WS message parse error: %s", e)


def load_config_prefix_klippylink(config):
    return KlippylinkRouter(config)


# ── NeoPixel ──────────────────────────────────────────────────────────────────

class KlippylinkNeopixel:
    def __init__(self, config):
        self.printer      = config.get_printer()
        self.name         = config.get_name().split()[-1]
        self.router_name  = config.get('router')
        self.node_name    = config.get('node')
        self.strip_name   = config.get('strip')
        self.count        = config.getint('count', 1)

        gcode = self.printer.lookup_object('gcode')
        gcode.register_command(
            f'SET_KLIPPYLINK_LED',
            self.cmd_SET_KLIPPYLINK_LED,
            desc=self.cmd_SET_KLIPPYLINK_LED_help,
        )

    cmd_SET_KLIPPYLINK_LED_help = "Set a KlippyLink NeoPixel LED colour"

    def cmd_SET_KLIPPYLINK_LED(self, gcmd):
        strip  = gcmd.get('STRIP', self.name)
        r      = gcmd.get_float('RED',   0.0, minval=0., maxval=1.)
        g      = gcmd.get_float('GREEN', 0.0, minval=0., maxval=1.)
        b      = gcmd.get_float('BLUE',  0.0, minval=0., maxval=1.)
        index  = gcmd.get_int('INDEX', -1)
        index  = None if index < 0 else index

        # Resolve which object to use (support multiple strips via STRIP= arg)
        try:
            neopixel_obj = self.printer.lookup_object(
                f'klippylink_neopixel {strip}')
        except Exception:
            neopixel_obj = self
        router = self.printer.lookup_object(
            f'klippylink {neopixel_obj.router_name}')
        router.set_neopixel(
            neopixel_obj.node_name, neopixel_obj.strip_name,
            r, g, b, index)

    def get_status(self, eventtime):
        return {"name": self.name, "count": self.count}


def load_config_prefix_klippylink_neopixel(config):
    return KlippylinkNeopixel(config)


# ── Endstop ───────────────────────────────────────────────────────────────────

class KlippylinkEndstop:
    def __init__(self, config):
        self.printer      = config.get_printer()
        self.reactor      = self.printer.get_reactor()
        self.name         = config.get_name().split()[-1]
        self.router_name  = config.get('router')
        self.node_name    = config.get('node')
        self.endstop_name = config.get('endstop')
        self._triggered   = False
        self._timer       = None

        gcode = self.printer.lookup_object('gcode')
        gcode.register_command(
            'QUERY_KLIPPYLINK_ENDSTOP',
            self.cmd_QUERY_KLIPPYLINK_ENDSTOP,
            desc="Query a KlippyLink endstop state",
        )

        self.printer.register_event_handler("klippy:connect",
                                             self._start_polling)
        # Also listen for push events from the WS stream
        self.printer.register_event_handler("klippylink:endstop_change",
                                             self._on_endstop_change)

    def _start_polling(self):
        self._timer = self.reactor.register_timer(
            self._poll, self.reactor.NOW)

    def _poll(self, eventtime):
        try:
            router = self.printer.lookup_object(f'klippylink {self.router_name}')
            state = router.query_endstop(self.node_name, self.endstop_name)
            if state is not None:
                self._triggered = state
        except Exception as e:
            logger.debug("Endstop poll error: %s", e)
        return eventtime + POLL_INTERVAL_S

    def _on_endstop_change(self, node, idx, triggered):
        # WS push events update state immediately (no poll lag)
        if node == self.node_name:
            self._triggered = triggered

    def cmd_QUERY_KLIPPYLINK_ENDSTOP(self, gcmd):
        name = gcmd.get('ENDSTOP', self.name)
        try:
            obj = self.printer.lookup_object(f'klippylink_endstop {name}')
        except Exception:
            obj = self
        gcmd.respond_info(
            f"KlippyLink endstop '{obj.endstop_name}' on '{obj.node_name}': "
            f"{'TRIGGERED' if obj._triggered else 'open'}"
        )

    def get_status(self, eventtime):
        return {
            "triggered": self._triggered,
            "node":      self.node_name,
            "endstop":   self.endstop_name,
        }


def load_config_prefix_klippylink_endstop(config):
    return KlippylinkEndstop(config)


# ── Klipper module entry points ───────────────────────────────────────────────

def load_config(config):
    return KlippylinkRouter(config)


def load_config_prefix(config):
    section = config.get_name().split()[0]
    dispatch = {
        "klippylink":          KlippylinkRouter,
        "klippylink_neopixel": KlippylinkNeopixel,
        "klippylink_endstop":  KlippylinkEndstop,
    }
    cls = dispatch.get(section)
    if cls is None:
        raise config.error(f"Unknown KlippyLink section: {section}")
    return cls(config)
