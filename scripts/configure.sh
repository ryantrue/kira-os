#!/usr/bin/env bash
set -euo pipefail

resolve_components() {
    local attempt
    for attempt in 1 2 3 4 5; do
        if idf.py reconfigure; then
            return 0
        fi
        if [ "$attempt" -lt 5 ]; then
            echo "Component graph expanded; reconfigure attempt $((attempt + 1))/5"
        fi
    done
    echo "Unable to resolve the ESP-IDF component graph after 5 attempts" >&2
    return 1
}

idf.py set-target esp32p4
resolve_components
idf.py gen-bmgr-config -c boards -b esp32_p4_wifi6_touch_lcd_4b

# Board Manager discovers board defaults only after generating the board layer.
# Rebuild sdkconfig so the board's custom 32 MiB partition table and hardware
# settings are actually applied before the final dependency convergence/build.
rm -f sdkconfig

# The generated board layer enables further conditional dependencies such as
# ESP-Hosted and the media pipeline. IDF persists each expansion in the lock
# file, so bounded retries allow the graph to converge without hiding errors.
resolve_components
