#!/bin/bash
# mkprofile.sh <ja|en> — 隔離プロファイル (out/home-<lang>) に最小限の settings.json を作る
set -euo pipefail
S="$(cd "$(dirname "$0")" && pwd)"; L="$1"; H="$S/out/home-$L"
rm -rf "$H"; mkdir -p "$H/Library/Preferences/Farman/farman" "$H/Library/Application Support"
cat > "$H/Library/Preferences/Farman/farman/settings.json" <<JSON
{
  "version": 6,
  "behavior": {
    "language": "$L",
    "singleInstance": false,
    "confirmOnExit": false,
    "autoUpdate": {"channel": "stable", "checkOnStartup": false},
    "whatsNewShownVersion": "1.0.1",
    "computeDirectorySizes": true,
    "defaultBookmarksInstalled": true,
    "persistHistory": false,
    "progressAutoClose": false,
    "defaultDeleteToTrash": true,
    "layoutMode": "dual"
  },
  "initialPaths": {
    "left":  {"customPath": "/tmp/farman-demo/left",         "mode": "custom"},
    "right": {"customPath": "/tmp/farman-demo/right/backup", "mode": "custom"}
  },
  "themes": {"mode": "light"},
  "log": {"toFile": false, "visible": false},
  "window": {
    "sizeMode": "custom", "customSize": {"width": 1200, "height": 668},
    "positionMode": "custom", "customPosition": {"x": 120, "y": 90}
  }
}
JSON
echo "$H"
