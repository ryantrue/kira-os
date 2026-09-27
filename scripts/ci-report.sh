#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out=build/kira-report; mkdir -p "$out"
fail(){ echo "error: $*" >&2; exit 1; }
if git ls-files --error-unmatch sdkconfig.defaults.local >/dev/null 2>&1; then fail "sdkconfig.defaults.local is committed"; fi
if [[ -f sdkconfig.defaults.local && -n "${CI:-}" ]]; then fail "sdkconfig.defaults.local present in CI"; fi
python3 scripts/check-partitions.py
grep -q '^CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y$' sdkconfig || fail "rollback is not set"
grep -R -q 'kira::boot::start' main/ || fail "main does not call kira::boot::start()"
! grep -R -E 'ota_1|0x15a0000|0x1a0000.*, *10M' main components || fail "old OTA layout referenced in code"
cp sdkconfig "$out/sdkconfig"; [[ -f dependencies.lock ]] && cp dependencies.lock "$out/dependencies.lock"
idf.py size > "$out/size.txt"; idf.py size-components > "$out/size-components.txt"
python3 - "$out" <<'PY'
import os,re,subprocess,sys
out=sys.argv[1]; idf=os.environ["IDF_PATH"]
def ps(v):
 m=re.fullmatch(r"(0x[0-9a-fA-F]+|\d+)([KkMm]?)",v.strip()); return int(m.group(1),0)*{"":1,"k":1024,"m":1048576}[m.group(2).lower()]
csv=subprocess.run([sys.executable,f"{idf}/components/partition_table/gen_esp32part.py","build/partition_table/partition-table.bin"],check=True,capture_output=True,text=True).stdout
apps=[ps(c[4]) for l in csv.splitlines() if l.strip() and not l.startswith("#") for c in [[x.strip() for x in l.split(",")]] if len(c)>=5 and c[1]=="app"]
slot=min(apps); used=os.path.getsize("build/kira_os.bin"); free=100*(slot-used)/slot
rec=os.path.getsize("recovery/build/kira_recovery.bin")
keys=re.compile(r"^(# )?CONFIG_(COMPILER_OPTIMIZATION_|ESP_DEFAULT_CPU_FREQ_MHZ|ESP_SYSTEM_EVENT_TASK_STACK_SIZE|ESP_MAIN_TASK_STACK_SIZE|FREERTOS_HZ|SPIRAM_XIP_FROM_PSRAM|BOOTLOADER_APP_ROLLBACK|ESP_COREDUMP|FREERTOS_CHECK_STACKOVERFLOW|LV_USE_SYSMON|LV_BUILD_EXAMPLES|ESP32P4_REV_MIN|ESP32P4_SELECTS_REV|.*PPA)")
cfg=[l.rstrip() for l in open("sdkconfig") if keys.match(l)]
with open(f"{out}/summary.md","w") as f:
 f.write("## Kira build report\n\n")
 f.write(f"`kira_os.bin`: {used:,} bytes, ota_0 {slot:,} bytes, **{free:.1f}% free**\n\n")
 f.write(f"`kira_recovery.bin`: {rec:,} bytes, recovery 2,097,152 bytes, **{100*(2097152-rec)/2097152:.1f}% free**\n\n")
 f.write("### Key sdkconfig values\n```\n"+"\n".join(cfg)+"\n```\n")
print(f"kira_os.bin {used} / {slot} bytes, {free:.1f}% free")
print(f"kira_recovery.bin {rec} / 2097152 bytes, {100*(2097152-rec)/2097152:.1f}% free")
if free<5:sys.exit("less than 5% free in ota_0")
PY
[[ -n "${GITHUB_STEP_SUMMARY:-}" ]] && cat "$out/summary.md" >> "$GITHUB_STEP_SUMMARY"
echo "Kira report: $out"
