# Kira Settings product override

Vendored from the Espressif Component Registry **brookesia_app_settings 0.8.3**,
the exact version in the failing product build's dependency lock.

- Upstream commit: `01939b5e58fd50d18339b1c35fb74c4e808962c7`
- Upstream path: `app/brookesia_app_settings`
- Registry component hash: `46dd5de734a74672203240420fd52967b3f61f9882b600813d24b1fccd3204a5`
- Archive URL: https://components.espressif.com/api/downloads/?object_type=component&object_id=08e3df60-15ca-4cd0-91e5-a678af740a8f
- License: Apache-2.0 (upstream notices retained).

`main/idf_component.yml` selects this directory with the Component Manager's
`override_path`. No generated managed component source is edited. System Super
remains an upstream dependency. Product changes add stock Settings navigation
and delegate Kira actions to `components/kira_settings`.

Settings JSON is embedded into the firmware using the supported `JsonString`
GUI descriptor. This makes new controls available after an app-only 0.2 -> 0.3
OTA while retaining the original LittleFS and its existing image assets. No
LittleFS partition rewrite or on-device source patching is needed.
