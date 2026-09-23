# Upstream updates

Kira composes upstream components (ESP-IDF, ESP-Brookesia, ESP Board Manager,
Waveshare BSP, ESP-Hosted, LVGL). The goal is to take upstream improvements
routinely without turning every update into a build emergency.

## Rules

1. **Lock, don't float.** `dependencies.lock` is committed. Every build, local
   or CI, uses exactly the resolved versions in it.
2. **One Brookesia line.** All `espressif/brookesia_*` components resolve to the
   same 0.8.x release line. Never pin one Brookesia package to an old patch
   while the others float; that combination produced the MIPI DSI callback
   mismatch against IDF 6.0.2.
3. **No forks.** Upstream code is not copied into the repo. If a fix is needed
   before upstream ships it, prefer, in order:
   - a build flag in the root `CMakeLists.txt` with a comment and an issue link;
   - a small patch file under `patches/` applied by a script, with the upstream
     issue/PR link in its header;
   - an `override_path` to a local copy, only as a last resort, removed as soon
     as upstream releases the fix.
4. **Updates are changes.** A dependency update is a reviewed commit that
   contains the new lock file and any code changes it required.

## Routine update

    . "$IDF_PATH/export.sh"
    bash scripts/update-deps.sh

Then flash, boot-test and check display, touch, audio capture and Wi-Fi on the
rev1.3 board, and commit `dependencies.lock` with a note of what was verified.

## Early warning

`.github/workflows/upstream-canary.yml` runs weekly (and on demand). It deletes
the lock, resolves the newest versions allowed by the manifests, and builds
against the moving `espressif/idf:release-v6.0` image. The job summary shows the
lock diff and the first errors; the resolved lock is uploaded as an artifact.

A red canary means the next update will need work. It does not affect main.

## Pending migrations

### ESP-IDF 6.0.1 -> 6.0.2

The CI baseline is 6.0.1. Moving to 6.0.2 previously failed on a Brookesia MIPI
DSI callback API mismatch caused by `brookesia_hal_adaptor` being pinned to an
older 0.8 patch than the rest of the Brookesia line. Migration plan:

1. Align all `brookesia_*` entries in `main/idf_component.yml` to one 0.8.x
   release line (drop individual old-patch pins).
2. Allow `>=6.0.1,<6.1` for `idf` in the manifest.
3. Switch the CI container to `espressif/idf:v6.0.2`.
4. Clean build; check whether the `hal/assert.h` `-Wattributes` workaround in
   the root CMakeLists.txt is still needed.
5. Hardware verification, then commit the new lock.

### Upstream issue to report

ESP Board Manager's public header `periph_i2s.h` includes the low-level
`hal/i2s_ll.h`. Any C++ consumer then inherits the IDF 6.0.x `hal/assert.h`
`[[noreturn]]` warning under GCC 15. Public headers should not expose LL
headers. Once fixed upstream, drop `-Wno-error=attributes` from the root
CMakeLists.txt.
