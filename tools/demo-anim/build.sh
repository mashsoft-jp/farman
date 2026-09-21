#!/bin/bash
# 撮影用の Swift ヘルパ (fkey / winid / axwin) を out/bin にビルドする
set -euo pipefail
S="$(cd "$(dirname "$0")" && pwd)"; mkdir -p "$S/out/bin"
for t in fkey winid axwin; do
  swiftc -O "$S/$t.swift" -o "$S/out/bin/$t" 2>&1 | grep -v 'warning' || true
done
ls "$S/out/bin"
