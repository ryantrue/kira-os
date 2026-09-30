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


def patch_settings_home(stage):
    """Add Kira to the real brookesia.general.settings home screen.

    This patches only the staged runtime package. The managed component remains
    untouched, so the build is reproducible and an upstream layout change fails
    loudly instead of silently producing a second Settings application.
    """
    home = stage / "apps" / "brookesia.general.settings" / "res" / "screens" / "settings_home.json"
    if not home.is_file():
        # System Core staging layouts have changed across 0.8.x; locate the
        # package by identity rather than assuming one generated directory.
        matches = list(stage.rglob("brookesia.general.settings/res/screens/settings_home.json"))
        if matches:
            home = matches[0]
        else:
            print("kira overrides: stock Settings home not staged yet, skipping")
            return

    data = json.loads(home.read_text(encoding="utf-8"))
    children = data["children"][0]["children"]
    main_list = next(
        child for child in children
        if child.get("id") == "main_list" and child.get("type") == "templateRef"
    )
    content = main_list["slots"]["content"]
    if any(item.get("id") == "kira" for item in content):
        return

    more = next((item for item in content if item.get("id") == "more"), None)
    if more is None:
        raise SystemExit("kira overrides: stock Settings 'more' row not found; update integration")

    kira_row = json.loads(json.dumps(more))
    kira_row["id"] = "kira"
    overrides = kira_row["overrides"]
    overrides["."]["events"] = [{"type": "clicked", "action": "settings.open.kira"}]
    overrides["title_box/title"]["labelProps"]["text"] = "Kira"
    overrides["value_box/value"]["labelProps"]["text"] = "Assistant"
    # Reuse an upstream-owned icon until Kira's settings resource package owns
    # its own image. This avoids introducing an unresolved image id.
    content.insert(content.index(more), kira_row)

    home.write_text(json.dumps(data, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"kira overrides: Kira entry added to stock Settings in {home.relative_to(stage)}")


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
    patch_settings_home(stage)


if __name__ == "__main__":
    main()
