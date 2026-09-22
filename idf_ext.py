"""Expose ESP Board Manager's idf.py action after dependencies are resolved."""

from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path


def _missing_board_manager(_target_name, _ctx, _args, **_kwargs):
    raise RuntimeError(
        "esp_board_manager is not resolved yet; run 'idf.py reconfigure' first"
    )


def _load(path: Path):
    spec = spec_from_file_location("kira_board_manager_ext", path)
    if spec is None or spec.loader is None:
        raise ImportError(f"Cannot load {path}")
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def action_extensions(base_actions: dict, project_path: str) -> dict:
    root = Path(project_path)
    candidates = (
        root / "managed_components" / "espressif__esp_board_manager" / "idf_ext.py",
        root / "components" / "esp_board_manager" / "idf_ext.py",
        root / "components" / "espressif__esp_board_manager" / "idf_ext.py",
    )
    extension = next((path for path in candidates if path.is_file()), None)
    if extension is None:
        return {
            "actions": {
                "gen-bmgr-config": {
                    "callback": _missing_board_manager,
                    "options": [],
                    "short_help": "Generate ESP Board Manager configuration files",
                }
            }
        }
    return _load(extension).action_extensions(base_actions, project_path)

