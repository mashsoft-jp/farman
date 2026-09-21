#!/bin/bash
# encode.sh <lang> <out.webp> [img2webp のオプション...] — out/frames-<lang> と durations.txt からアニメ WebP を作る
set -euo pipefail
S="$(cd "$(dirname "$0")" && pwd)"; L="$1"; OUT="$2"; shift 2
args=(-loop 0 "$@")
while IFS=$'\t' read -r f d _; do args+=(-d "$d" "$S/out/frames-$L/$f"); done < "$S/out/frames-$L/durations.txt"
img2webp "${args[@]}" -o "$OUT" >/dev/null
ls -la "$OUT" | awk '{printf "%s  %.0f KB\n", $9, $5/1024}'
