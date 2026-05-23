#!/usr/bin/env bash
# KlippyLink Klipper extension installer
# Run on the Raspberry Pi (or any Linux host running Klipper):
#   bash install.sh
set -euo pipefail

KLIPPER_DIR="${KLIPPER_DIR:-${HOME}/klipper}"
KLIPPY_EXTRAS="${KLIPPER_DIR}/klippy/extras"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "============================================"
echo " KlippyLink Klipper Extension Installer"
echo "============================================"

# ── Sanity checks ──────────────────────────────────────────────────────────

if [ ! -d "${KLIPPER_DIR}" ]; then
  echo "ERROR: Klipper directory not found at ${KLIPPER_DIR}"
  echo "       Set KLIPPER_DIR=/path/to/klipper and re-run."
  exit 1
fi

if [ ! -d "${KLIPPY_EXTRAS}" ]; then
  echo "ERROR: klippy/extras not found inside ${KLIPPER_DIR}"
  exit 1
fi

# ── Install Python dependency (websocket-client is optional but recommended) ─

echo ""
echo "Installing Python dependencies..."
VENV="${KLIPPER_DIR}/klippy-env"
if [ -d "${VENV}" ]; then
  "${VENV}/bin/pip" install --quiet websocket-client || true
else
  pip3 install --quiet websocket-client --user || true
fi

# ── Copy extra ─────────────────────────────────────────────────────────────

echo "Installing klippylink.py → ${KLIPPY_EXTRAS}/"
cp -f "${SCRIPT_DIR}/klippylink.py" "${KLIPPY_EXTRAS}/klippylink.py"

# ── Symlink (alternative) — easier for development ─────────────────────────
# ln -sf "${SCRIPT_DIR}/klippylink.py" "${KLIPPY_EXTRAS}/klippylink.py"

echo ""
echo "Done!  Add the following to your printer.cfg and restart Klipper:"
echo ""
echo "  [klippylink router]"
echo "  host: <router-ip-address>"
echo "  port: 8765"
echo ""
echo "  # Example NeoPixel strip on a node:"
echo "  [klippylink_neopixel toolhead_leds]"
echo "  router: router"
echo "  node:   toolhead_node"
echo "  strip:  status_leds"
echo "  count:  8"
echo ""
echo "  # Example endstop on a node:"
echo "  [klippylink_endstop x_min]"
echo "  router:   router"
echo "  node:     toolhead_node"
echo "  endstop:  x_min"
echo ""
echo "See klippylink.cfg.example for full reference."
