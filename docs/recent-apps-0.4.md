# Kira OS 0.4 — Recent Apps integration

## Verified upstream behavior

The pinned Brookesia System Super 0.8.x shell owns the bottom gesture indicator and foreground-app exit gesture.
Upstream exposes app lifecycle state through System Core (`AppInfo::state`) and app control through
`SystemApi::get_active_app()`, `list_apps()`, `start_app()`, and `stop_app()`.

The upstream shell does **not** expose a Recent Apps/task-switcher surface in the API used by Kira OS.
Its bottom gesture path is an app-exit path (`trigger_gesture_exit()`), so Kira must not pretend that
the stock launcher is a recents view.

## 0.4 design

- Keep System Core as the only app lifecycle owner.
- Track an MRU list of visible app IDs from foreground transitions; never run a second app manager.
- Recent cards start/resume apps through System Core.
- Do not promise concurrent rendering: a card represents lifecycle history/state, not a live app snapshot.
- Bottom-up gesture while a regular app is foreground should open Recent Apps instead of immediately
  discarding lifecycle history.
- Home/launcher remains a separate destination.
- The shell change must be a narrow, version-pinned product override. Do not fork unrelated System Super code.
- If the pinned component does not expose a stable hook, keep the existing exit gesture until the override
  is compile-tested; never replace it with an unverified gesture implementation.

## Acceptance

1. Kira, Home Assistant, Settings and Files can appear in MRU after use.
2. Selecting a card delegates to System Core `start_app()`.
3. Closed/uninstalled apps disappear safely.
4. No duplicate app lifecycle owner exists.
5. Existing launcher and status overlay continue to work.
6. Firmware CI is green before physical gesture testing.
7. Gesture behavior is only marked verified after testing on the ESP32-P4 touch panel.
