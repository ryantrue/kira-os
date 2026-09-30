#!/usr/bin/env python3
"""Embed the pinned Settings GUI documents, preserving package-relative images.

App-only recovery updates retain LittleFS. JSON is therefore part of the app
image; existing stock image assets continue to load from the package directory.
"""
import json
import pathlib
import sys


def embed(root):
    def asset(path):
        data = json.loads((root / path).read_text(encoding="utf-8"))
        if data.get("type") == "imageSet":
            for item in data["images"]:
                item["src"] = (pathlib.PurePosixPath("res") / pathlib.PurePosixPath(path).parent / item["src"]).as_posix()
        return data
    data = json.loads((root / "root.json").read_text(encoding="utf-8"))
    data["assets"] = [asset(path) for path in data["assets"]]
    for variant in data.get("variants", []):
        variant["assets"] = [asset(path) for path in variant["assets"]]
    return json.dumps(data, ensure_ascii=False, separators=(",", ":"))


if __name__ == "__main__":
    text = embed(pathlib.Path(sys.argv[1]))
    if ')KIRA_JSON"' in text:
        raise SystemExit("Embedded JSON raw-string delimiter collision")
    pathlib.Path(sys.argv[2]).write_text(
        '#pragma once\ninline constexpr const char KIRA_SETTINGS_RESOURCES[] = R"KIRA_JSON(' + text + ')KIRA_JSON";\n',
        encoding="utf-8")
