#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ -n "${SDKCONFIG_DEFAULTS:-}" ]]; then
  echo "error: SDKCONFIG_DEFAULTS is set in the environment: ${SDKCONFIG_DEFAULTS}" >&2
  echo "       Kira defines its defaults layers in CMakeLists.txt. Unset the variable." >&2
  exit 1
fi

resolve_components() {
  local attempt
  for attempt in 1 2 3 4 5; do
    if idf.py reconfigure; then return 0; fi
    if [[ "$attempt" -lt 5 ]]; then
      echo "Component graph expanded; reconfigure attempt $((attempt + 1))/5"
    fi
  done
  echo "Unable to resolve the ESP-IDF component graph after 5 attempts" >&2
  return 1
}

verify_revision_profile() {
  local missing=0 sym
  for sym in '^CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y$' '^CONFIG_ESP32P4_REV_MIN_100=y$' '^CONFIG_SPIRAM_SPEED_200M=y$'; do
    if ! grep -q "$sym" sdkconfig; then
      echo "error: rev1.3 profile lost after configure: ${sym}" >&2
      missing=1
    fi
  done
  return "$missing"
}

idf.py set-target esp32p4
resolve_components
idf.py gen-bmgr-config -c boards -b esp32_p4_wifi6_touch_lcd_4b
rm -f sdkconfig
resolve_components
verify_revision_profile
echo "Kira configure: OK (rev1.3 profile verified)"
