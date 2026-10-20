#!/usr/bin/env python3
"""Validate Frame-Guard's metadata, setting references and build version alignment.

This script deliberately uses only the Python standard library so it can run locally
and in GitHub Actions without an Android SDK or Geode toolchain.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MOD_PATH = ROOT / "geode-android" / "mod.json"
SOURCE_PATH = ROOT / "geode-android" / "src" / "main.cpp"
CMAKE_PATH = ROOT / "geode-android" / "CMakeLists.txt"
README_PATH = ROOT / "geode-android" / "README.md"
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "build-android-geode.yml"


def require(condition: bool, message: str) -> None:
    if not condition:
        print(f"Frame-Guard validation failed: {message}", file=sys.stderr)
        raise SystemExit(1)


def main() -> None:
    try:
        mod = json.loads(MOD_PATH.read_text(encoding="utf-8"))
        source = SOURCE_PATH.read_text(encoding="utf-8")
        cmake = CMAKE_PATH.read_text(encoding="utf-8")
        readme = README_PATH.read_text(encoding="utf-8")
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
    except (OSError, json.JSONDecodeError) as exc:
        require(False, f"unable to read project inputs: {exc}")
        return

    version = mod.get("version", "")
    require(bool(re.fullmatch(r"v\d+\.\d+\.\d+", version)),
            "mod.json version must use vMAJOR.MINOR.PATCH")
    require(mod.get("id") == "sfj3861.frame-guard", "unexpected mod id")
    require(mod.get("geode") == "5.11.2", "unexpected Geode SDK target")
    require(mod.get("gd", {}).get("android") == "2.2081",
            "unexpected Geometry Dash Android target")

    cmake_match = re.search(
        r"project\(FrameGuardGeode VERSION (\d+\.\d+\.\d+) LANGUAGES CXX\)",
        cmake,
    )
    require(cmake_match is not None, "CMake project version not found")
    require(cmake_match.group(1) == version[1:],
            f"CMake version {cmake_match.group(1)} does not match {version}")

    settings = mod.get("settings", {})
    require(isinstance(settings, dict), "mod.json settings must be an object")
    referenced = set(re.findall(
        r'(?:getSettingValue|setSettingValue)\s*<[^>]+>\s*\(\s*"([^"]+)"',
        source,
    ))
    referenced.update(re.findall(
        r'KeybindSettingPressedEventV3\(Mod::get\(\),\s*"([^"]+)"',
        source,
    ))
    missing = sorted(referenced.difference(settings))
    require(not missing, f"source references undeclared settings: {', '.join(missing)}")

    scheduler = settings.get("input-scheduler-enabled", {})
    require(scheduler.get("type") == "bool", "scheduler setting must be boolean")
    require(scheduler.get("default") is False,
            "experimental input scheduler must be disabled by default")
    delay = settings.get("max-delay-ms", {})
    require(delay.get("min") == 0 and delay.get("max") == 200,
            "max-delay-ms must remain constrained to 0..200")

    require("m_objectLayer->addChild(m_fields->overlay, 9999)" in source,
            "overlay must remain attached to the game object layer")
    require("safeCollisionRect(obj)" in source,
            "rendering and input prediction must use the safe rectangle helper")
    require("m_cachedObjectEntries" in source and
            "objectArray->objectAtIndex(entry.first)" in source,
            "dynamic-object cache identity checks are missing")

    require(f"Versión de desarrollo:** {version}." in readme,
            "README version does not match mod.json")
    require(f"Frame-Guard-Android64-{version}" in workflow,
            "workflow artifact name does not match mod.json version")
    require(f"Mod version: {version}." in workflow,
            "workflow release notes do not match mod.json version")

    print(
        f"Frame-Guard {version}: project metadata, {len(settings)} settings, "
        f"source references, cache invariants and CI version labels passed."
    )


if __name__ == "__main__":
    main()
