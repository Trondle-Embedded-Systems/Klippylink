"""
KlippyLink Klipper extra — bridges Klipper to KlippyLink BLE nodes via the
linux-host API server (default http://localhost:8080).

Config sections:

  [klippylink router_name]
  api_url: http://localhost:8080   # linux-host API (default)

  [klippylink_neopixel my_leds]
  router: router_name
  node:   toolhead_node            # node device_name from its config.json
  strip:  status_leds              # strip name from node config.json
  count:  8

  [klippylink_endstop x_min]
  router: router_name
  node:   toolhead_node
  endstop: x_min

  [klippylink_heater chamber_heater]
  router: router_name
  node:   toolhead_node
  target_temp: 0.0
  kp: 1.0
  ki: 0.1
  kd: 0.01

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

POLL_INTERVAL_S = 0.1


# ── Router / API gateway ──────────────────────────────────────────────────────

class KlippylinkRouter:
    def __init__(self, config):
        self.printer  = config.get_printer()
        self.name     = config.get_name().split()[-1]
        self._base    = config.get('api_url', 'http://localhost:8080').rstrip('/')
        self._lock    = threading.Lock()

        self.printer.register_event_handler("klippy:connect",
                                             self._handle_connect)

    def _handle_connect(self):
        try:
            info = self._get("/api/info")
            logger.info("KlippyLink host '%s' connected: %s", self.name, info)
        except Exception as e:
            raise self.printer.config_error(
                f"Cannot reach KlippyLink host '{self.name}' "
                f"at {self._base}: {e}"
            )

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

    def set_led_zone(self, node, zone_body: dict):
        try:
            self._post(f"/api/nodes/{node}/leds/zones", zone_body)
        except Exception as e:
            logger.warning("set_led_zone failed: %s", e)

    def query_endstop(self, node, endstop):
        try:
            data = self._get(f"/api/nodes/{node}/endstops/{endstop}")
            return data.get("triggered", False)
        except Exception as e:
            logger.warning("query_endstop failed: %s", e)
            return None

    def set_heater(self, node, target_temp, kp, ki, kd):
        try:
            self._post(f"/api/nodes/{node}/heater/set", {
                "target_temp": target_temp,
                "kp": kp, "ki": ki, "kd": kd,
            })
        except Exception as e:
            logger.warning("set_heater failed: %s", e)


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
            'SET_KLIPPYLINK_LED',
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


# ── Heater ────────────────────────────────────────────────────────────────────

class KlippylinkHeater:
    """
    Virtual heater sensor backed by a KlipLink node's PID heater.
    Reports temperature read from HEATER_STATE notifications (polled via API).
    """

    def __init__(self, config):
        self.printer     = config.get_printer()
        self.reactor     = self.printer.get_reactor()
        self.name        = config.get_name().split()[-1]
        self.router_name = config.get('router')
        self.node_name   = config.get('node')
        self.target_temp = config.getfloat('target_temp', 0.0)
        self.kp          = config.getfloat('kp', 1.0)
        self.ki          = config.getfloat('ki', 0.1)
        self.kd          = config.getfloat('kd', 0.01)
        self._temp       = 0.0
        self._duty       = 0

        self.printer.register_event_handler("klippy:connect",
                                             self._handle_connect)

    def _handle_connect(self):
        router = self.printer.lookup_object(f'klippylink {self.router_name}')
        if self.target_temp > 0:
            router.set_heater(self.node_name, self.target_temp,
                              self.kp, self.ki, self.kd)
        self.reactor.register_timer(self._poll_status, self.reactor.NOW)

    def _poll_status(self, eventtime):
        try:
            router = self.printer.lookup_object(f'klippylink {self.router_name}')
            data = router._get(f"/api/nodes/{self.node_name}/status")
            caps = data.get("capabilities", {})
            self._temp = caps.get("temp_current", self._temp)
            self._duty = caps.get("duty", self._duty)
        except Exception as e:
            logger.debug("Heater poll error: %s", e)
        return eventtime + 0.5

    def set_temp(self, degrees):
        self.target_temp = degrees
        try:
            router = self.printer.lookup_object(f'klippylink {self.router_name}')
            router.set_heater(self.node_name, degrees,
                              self.kp, self.ki, self.kd)
        except Exception as e:
            logger.warning("set_temp failed: %s", e)

    def get_status(self, eventtime):
        return {
            "temperature": self._temp,
            "target":      self.target_temp,
            "duty":        self._duty,
        }


def load_config_prefix_klippylink_heater(config):
    return KlippylinkHeater(config)


# ── Klipper module entry points ───────────────────────────────────────────────

def load_config(config):
    return KlippylinkRouter(config)


def load_config_prefix(config):
    section = config.get_name().split()[0]
    dispatch = {
        "klippylink":         KlippylinkRouter,
        "klippylink_neopixel": KlippylinkNeopixel,
        "klippylink_endstop":  KlippylinkEndstop,
        "klippylink_heater":   KlippylinkHeater,
    }
    cls = dispatch.get(section)
    if cls is None:
        raise config.error(f"Unknown KlippyLink section: {section}")
    return cls(config)
