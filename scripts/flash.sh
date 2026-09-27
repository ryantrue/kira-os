#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
usage(){ echo "usage: scripts/flash.sh PORT [--erase]" >&2; exit 2; }
port="${1:-}"; [[ -n "$port" ]]||usage; erase=0
case "${2:-}" in "") ;; --erase) erase=1 ;; *) usage ;; esac
python3 scripts/check-partitions.py
for f in build/flash_args build/kira_os.bin recovery/build/kira_recovery.bin recovery/build/partition_table/partition-table.bin; do
 [[ -f "$f" ]]||{ echo "missing $f" >&2; exit 1; }
done
recovery_offset="$(python3 scripts/check-partitions.py --offset recovery)"
python3 - "$recovery_offset" <<'PY'
import sys
off=sys.argv[1]; lines=open("build/flash_args").read().splitlines(); out=[]
for line in lines:
 if line.endswith("partition_table/partition-table.bin"):line=f"{line.split()[0]} ../recovery/build/partition_table/partition-table.bin"
 out.append(line)
out.append(f"{off} ../recovery/build/kira_recovery.bin")
open("build/kira_system_flash_args","w").write("\n".join(out)+"\n")
PY
cd build
if ((erase));then python -m esptool --chip esp32p4 -p "$port" erase_flash;fi
python -m esptool --chip esp32p4 -p "$port" -b 460800 write_flash @kira_system_flash_args
