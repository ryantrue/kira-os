#!/usr/bin/env python3
"""Patch the resolved Brookesia Settings component for Kira OS.

The component manager owns managed_components. This script is intentionally
idempotent and version-gated: CI fails if Brookesia 0.8.x changes the source
anchors instead of silently shipping a half-integrated Settings UI.
"""
from __future__ import annotations
import pathlib, sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
settings = root / "managed_components" / "espressif__brookesia_app_settings"
if not settings.is_dir():
    raise SystemExit(f"Kira: resolved Brookesia Settings not found: {settings}")

internal = settings / "src/private/settings_app_internal.hpp"
app = settings / "src/settings_app.cpp"
flow = settings / "package/res/flows/content.json"

def replace_once(path: pathlib.Path, old: str, new: str) -> None:
    text = path.read_text()
    if new in text:
        return
    if text.count(old) != 1:
        raise SystemExit(f"Kira: upstream Settings anchor changed in {path}: {old!r}")
    path.write_text(text.replace(old, new, 1))

replace_once(internal,
    'inline constexpr const char *PAGE_DEBUG = "debug";',
    'inline constexpr const char *PAGE_DEBUG = "debug";\n'
    'inline constexpr const char *PAGE_KIRA = "kira";\n'
    'inline constexpr const char *ACTION_OPEN_KIRA = "settings.open.kira";\n'
    'inline constexpr const char *ACTION_BACK_KIRA = "settings.back.kira";')

replace_once(internal,
    'inline constexpr std::array<const char *, 18> NAVIGATION_ACTIONS = {',
    'inline constexpr std::array<const char *, 20> NAVIGATION_ACTIONS = {')
replace_once(internal,
    '    ACTION_OPEN_DEBUG,\n};',
    '    ACTION_OPEN_DEBUG,\n'
    '    ACTION_OPEN_KIRA,\n'
    '    ACTION_BACK_KIRA,\n};')

replace_once(app,
    'static constexpr std::array<NavigationTarget, 18> NAVIGATION_TARGETS = {',
    'static constexpr std::array<NavigationTarget, 20> NAVIGATION_TARGETS = {')
replace_once(app,
    '    NavigationTarget{ACTION_OPEN_DEBUG, PAGE_DEBUG},\n};',
    '    NavigationTarget{ACTION_OPEN_DEBUG, PAGE_DEBUG},\n'
    '    NavigationTarget{ACTION_OPEN_KIRA, PAGE_KIRA},\n'
    '    NavigationTarget{ACTION_BACK_KIRA, PAGE_HOME},\n};')

replace_once(flow,
    '        "debug"\n    ],',
    '        "debug",\n        "kira"\n    ],')
replace_once(flow,
    '        {\n            "from": [],\n            "action": "settings.back.debug",\n            "to": "my_device"\n        }\n',
    '        {\n            "from": [],\n            "action": "settings.back.debug",\n            "to": "my_device"\n        },\n'
    '        {\n            "from": [],\n            "action": "settings.open.kira",\n            "to": "kira"\n        },\n'
    '        {\n            "from": [],\n            "action": "settings.back.kira",\n            "to": "settings_home"\n        }\n')

print("Kira: Brookesia Settings navigation patched")
