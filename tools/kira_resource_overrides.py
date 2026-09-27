#!/usr/bin/env python3
"""Apply Kira's overrides to the staged Brookesia System Super resources.

Brookesia stages its shell resources into littlefs/ at build time; they are
managed component files and must not be edited in managed_components/.
This script patches the staged copy right before the LittleFS image is built.

Keyboard: the 720x720 panel falls into System Super's "default" constants
variant, whose keyboard is sized for small screens (166 dp tall, 16 sp keys).
Kira uses the proportions of the 1024x600 variant instead, which Brookesia
ships and tests, adjusted to the square screen.

    tools/kira_resource_overrides.py <littlefs stage dir>
"""
import json
import pathlib
import sys

KEYBOARD_720X720 = {
    "iconSize": "30dp",
    "marginX": "10dp",
    "inputY": "12dp",
    "marginBottom": "10dp",
    "panelHeight": "392dp",
    "panelPadding": "10dp",
    "panelGap": "10dp",
    "inputHeight": "52dp",
    "inputFontSize": "22sp",
    "keyFontSize": "24sp",
    "eyeButtonSize": "44dp",
    "eyeIconSize": "28dp",
    "eyeInset": "6dp",
    "popoverHeight": "84dp",
    "keyboardHeight": "330dp",
}


def find_keyboard(node):
    """Return the dict that holds the keyboard constants, wherever it is nested."""
    if isinstance(node, dict):
        keyboard = node.get("keyboard")
        if isinstance(keyboard, dict) and "keyboardHeight" in keyboard:
            return keyboard
        for value in node.values():
            found = find_keyboard(value)
            if found is not None:
                return found
    elif isinstance(node, list):
        for value in node:
            found = find_keyboard(value)
            if found is not None:
                return found
    return None


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    stage = pathlib.Path(sys.argv[1])
    constants = stage / "system" / "super" / "shell" / "constants" / "default.json"
    if not constants.is_file():
        print(f"kira overrides: {constants} not staged yet, skipping")
        return
    data = json.loads(constants.read_text(encoding="utf-8"))
    keyboard = find_keyboard(data)
    if keyboard is None:
        sys.exit(f"kira overrides: keyboard constants not found in {constants}; "
                 "System Super changed its resource layout, update this script")
    unknown = sorted(set(KEYBOARD_720X720) - set(keyboard))
    if unknown:
        print(f"kira overrides: warning, keys not present upstream: {', '.join(unknown)}")
    keyboard.update(KEYBOARD_720X720)
    constants.write_text(json.dumps(data, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"kira overrides: keyboard sized for 720x720 in {constants.relative_to(stage)}")


if __name__ == "__main__":
    main()
