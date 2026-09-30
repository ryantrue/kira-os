#!/usr/bin/env python3
"""Assemble a self-describing, checked system flash set after both builds pass."""
import hashlib
import json
import pathlib
import shlex
import shutil
import struct
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "build/installation-candidate"
FILES = [
    (0x2000, "build/bootloader/bootloader.bin", "bootloader.bin", 0xE000),
    (0x10000, "recovery/build/partition_table/partition-table.bin", "partition-table.bin", 0x1000),
    (0x91000, "build/ota_data_initial.bin", "ota_data_initial.bin", 0x2000),
    (0x1A0000, "recovery/build/kira_recovery.bin", "kira_recovery.bin", 0x200000),
    (0x3A0000, "build/kira_os.bin", "kira_os.bin", 0x1000000),
    (0x13A0000, "build/littlefs_data.bin", "littlefs_data.bin", 0xA00000),
]


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def check_image(data, name, version):
    require(len(data) >= 24 and data[0] == 0xE9, f"{name}: bad ESP image")
    require(struct.unpack_from("<H", data, 12)[0] == 18, f"{name}: not ESP32-P4")
    minimum, maximum = struct.unpack_from("<HH", data, 15)
    require(minimum <= 103 and (maximum == 0 or maximum >= 103),
            f"{name}: excludes rev1.3 ({minimum}..{maximum})")
    if name != "bootloader.bin":
        require(len(data) >= 288 and struct.unpack_from("<I", data, 32)[0] == 0xABCD5432,
                f"{name}: no application descriptor")
        built_version = data[48:80].split(b"\0")[0].decode()
        require(built_version.split("+")[0] == version, f"{name}: version {built_version} != {version}")
        project = data[80:112].split(b"\0")[0].decode()
        expected_project = "kira_recovery" if name == "kira_recovery.bin" else "kira_os"
        require(project == expected_project, f"{name}: project {project} != {expected_project}")
    return {"min_chip_revision": minimum, "max_chip_revision": maximum}


def main():
    subprocess.run(["python3", str(ROOT / "scripts/check-partitions.py")], check=True)
    version = (ROOT / "version.txt").read_text().strip()
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    for cfg in (ROOT / "sdkconfig", ROOT / "recovery/sdkconfig"):
        content = set(cfg.read_text().splitlines())
        for symbol in ("CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y", "CONFIG_ESP32P4_REV_MIN_100=y",
                       "CONFIG_PARTITION_TABLE_OFFSET=0x10000", "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y"):
            require(symbol in content, f"{cfg}: missing {symbol}")
    config = set((ROOT / "sdkconfig").read_text().splitlines())
    require("CONFIG_SPIRAM_SPEED_200M=y" in config, "PSRAM 200 MHz profile lost")
    OUT.mkdir(parents=True, exist_ok=True)
    manifest = {"version": version, "commit": commit, "board": "Waveshare ESP32-P4-WIFI6-Touch-LCD-4B",
                "chip": "esp32p4", "revision": "1.3/pre-v3", "hardware_verified": False, "files": []}
    for offset, source, name, limit in FILES:
        data = (ROOT / source).read_bytes()
        require(0 < len(data) <= limit, f"{name}: {len(data)} bytes exceeds slot {limit}")
        info = {"name": name, "offset": f"0x{offset:08X}", "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest()}
        if name in ("bootloader.bin", "kira_recovery.bin", "kira_os.bin"):
            info.update(check_image(data, name, version))
        (OUT / name).write_bytes(data)
        manifest["files"].append(info)
    # The system table must contain the factory recovery entry, not the app's
    # same-address data placeholder. Validate actual generated binary entries.
    table = (OUT / "partition-table.bin").read_bytes()
    entries = {}
    for pos in range(0, len(table) - 31, 32):
        magic, kind, subtype, offset, size, label, flags = struct.unpack_from("<HBBII16sI", table, pos)
        if magic != 0x50AA:
            break
        entries[label.split(b"\0")[0].decode()] = (kind, subtype, offset, size)
    require(entries.get("recovery") == (0, 0, 0x1A0000, 0x200000), "Not the recovery system partition table")
    require(entries.get("ota_0") == (0, 16, 0x3A0000, 0x1000000), "ota_0 layout mismatch")
    require(entries.get("littlefs_data", (0, 0, 0, 0))[2:] == (0x13A0000, 0xA00000), "LittleFS layout mismatch")
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (OUT / "SHA256SUMS").write_text("".join(f"{f['sha256']}  {f['name']}\n" for f in manifest["files"]))
    app = next(f for f in manifest["files"] if f["name"] == "kira_os.bin")
    (OUT / "kira_os.bin.sha256").write_text(app["sha256"] + "\n")
    flags = (ROOT / "build/flash_args").read_text().splitlines()[0]
    # Keep the generated build's mode/frequency; only relocate image filenames.
    require(all(word.startswith("--") or word in ("qio", "dio", "qout", "dout", "20m", "40m", "80m", "120m", "32MB", "keep", "detect")
                for word in shlex.split(flags)), "Unexpected flash flags; inspect build/flash_args")
    (OUT / "flash_args").write_text(flags + "\n" +
        "".join(f"{f['offset']} {f['name']}\n" for f in manifest["files"]))
    shutil.copyfile(ROOT / "dependencies.lock", OUT / "dependencies.lock")
    shutil.copyfile(ROOT / "docs/installation-candidate.md", OUT / "INSTALL.md")
    print(f"Checked installation candidate: {OUT} ({commit})")


if __name__ == "__main__":
    main()
