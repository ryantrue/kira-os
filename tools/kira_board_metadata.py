#!/usr/bin/env python3
"""Generate Kira read-only pin metadata and SD settings from board sources.

No pin number belongs in platform application code. Unknown wiring is never
assumed available for output. ESP-Hosted and other IDF pins come from the actual
resolved sdkconfig, including defaults contributed by managed components.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re


def read_yaml(path):
    # ruamel.yaml ships with ESP-IDF Component Manager; PyYAML is accepted for
    # ordinary host tests. No new firmware build dependency is introduced.
    try:
        from ruamel.yaml import YAML
        return YAML(typ="safe").load(path.read_text())
    except ImportError:
        import yaml
        return yaml.safe_load(path.read_text())


def generate(board, config):
    pins = {}

    def add(number, function):
        if isinstance(number, int) and not isinstance(number, bool) and 0 <= number < 64:
            pins.setdefault(number, set()).add(function)

    def walk(value, owner, parent=""):
        if not isinstance(value, dict):
            return
        for key, child in value.items():
            if isinstance(child, dict):
                walk(child, owner, key)
            elif isinstance(child, list):
                for item in child:
                    walk(item, owner, key)
            elif parent == "pins" or key in {"pin", "gpio_num", "reset_gpio_num", "rst_gpio_num", "int_gpio_num"}:
                add(child, f"{owner}: {key}")

    devices = read_yaml(board / "board_devices.yaml")["devices"]
    for name, key in (("board_devices.yaml", "devices"), ("board_peripherals.yaml", "peripherals")):
        for item in read_yaml(board / name)[key]:
            walk(item.get("config", {}), item["name"])
    # Board-owned bring-up code may reserve additional GPIOs outside YAML.
    for number in re.findall(r"GPIO_NUM_(\d+)", (board / "setup_device.c").read_text()):
        add(int(number), "board setup")
    # Include actual host transport pins, reset pins, console, etc. Skip boolean
    # selectors and slave/C6 wiring: those numbers are not P4 GPIOs.
    pattern = r"^#define (CONFIG_\w*(?:GPIO|PIN)\w*) (-?\d+)\s*$"
    for symbol, value in re.findall(pattern, config.read_text(), re.MULTILINE):
        if symbol.startswith("CONFIG_SLAVE_") or symbol.endswith(("_EN", "_ENABLE", "_ENABLED")):
            continue
        if not re.search(r"(?:GPIO|PIN)(?:_NUM)?$|(?:GPIO|PIN)_(?:CLK|CMD|D[0-3]|RESET|SLAVE_RESET|RST|MOSI|MISO|SCLK|CS|HANDSHAKE|DATA_READY)$", symbol):
            continue
        add(int(value), symbol.removeprefix("CONFIG_").lower())
    sd = next(item for item in devices if item["name"] == "fs_sdcard")["config"]["sub_config"]
    lines = ["// Generated from board descriptors and resolved sdkconfig. Do not edit.",
             "#pragma once", "namespace kira::platform::board_metadata {",
             "struct Pin { int number; const char *function; };", "inline constexpr Pin pins[] = {"]
    lines += [f"    {{{number}, {json.dumps(', '.join(sorted(functions)))}}}," for number, functions in sorted(pins.items())]
    lines += ["};", f"inline constexpr int sd_slot = {sd['slot']};",
              f"inline constexpr int sd_frequency = {sd['frequency']};",
              f"inline constexpr int sd_bus_width = {sd['bus_width']};",
              f"inline constexpr int sd_ldo_channel = {sd['ldo_chan_id']};"]
    for key, number in sd["pins"].items():
        lines.append(f"inline constexpr int sd_{key} = {int(number)};")
    lines.append("}  // namespace kira::platform::board_metadata\n")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("board", type=Path)
    parser.add_argument("config", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    content = generate(args.board, args.config)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text() != content:
        args.output.write_text(content)


if __name__ == "__main__":
    main()
