#!/usr/bin/env bash
# images/icon.{png,svg} から、macOS .app バンドル用のアイコン (.icns) を生成する。
# 入力ソースは以下の優先順位で自動選択する:
#   1. images/icon.png (1024x1024 以上を推奨。PSD からの書き出し想定)
#   2. images/icon.svg (PNG が無ければフォールバック)
# PNG のほうが Photoshop 等で作ったグラデーション・影などをそのまま出せるため
# 通常はこちらを優先。SVG しか無い場合は sips でラスタライズする。
#
#   出力:
#     images/icon.icns          (macOS .app の Contents/Resources/icon.icns 用)
#
# macOS のアイコンは Apple HIG に合わせた「白角丸 + 中に絵」のデザイン
# (images/icon.png) をそのまま使う。
#
# Windows (.ico) / Linux (icon-256/512/1024.png) は別系統で、
# 「背景なし (透過) + 白フチ」のデザインを tools/make_transparent_icon.py が
# images/icon-no-shadow.png から生成する。OS の壁紙やタスクバー上に直接
# 載るため、白角丸の台紙を付けず、暗背景でも視認できるよう白フチを付ける。
# アイコン素材を更新したら、本スクリプト (macOS) と make_transparent_icon.py
# (Windows/Linux) の両方を実行すること。
#
# 主な依存:
#   - macOS 標準: sips, iconutil (これだけで .icns まで生成できる)
#
# アイコン素材を更新したら本スクリプトを手で実行して、生成物をリポジトリに
# コミットすること (CMake からは自動実行しない、ビルドのたびに走らせると
# 無駄に時間がかかるため)。

set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PNG_SRC="$REPO_DIR/images/icon.png"
SVG_SRC="$REPO_DIR/images/icon.svg"
OUT_DIR="$REPO_DIR/images"

# 入力ソースを決定 (PNG を優先)
if [[ -f "$PNG_SRC" ]]; then
  SOURCE="$PNG_SRC"
  SOURCE_KIND="PNG"
elif [[ -f "$SVG_SRC" ]]; then
  SOURCE="$SVG_SRC"
  SOURCE_KIND="SVG"
else
  echo "ERROR: Neither $PNG_SRC nor $SVG_SRC found." >&2
  exit 1
fi

if ! command -v sips >/dev/null 2>&1 || ! command -v iconutil >/dev/null 2>&1; then
  echo "ERROR: This script requires sips and iconutil (macOS built-ins)." >&2
  exit 1
fi

# ソースが正方形でない場合は警告 (アイコンは正方形必須)
DIMS="$(sips -g pixelWidth -g pixelHeight "$SOURCE" 2>/dev/null \
        | awk '/pixelWidth/ {w=$2} /pixelHeight/ {h=$2} END {print w"x"h}')"
SRC_W="${DIMS%x*}"
SRC_H="${DIMS#*x}"
if [[ -n "$SRC_W" && -n "$SRC_H" && "$SRC_W" != "$SRC_H" ]]; then
  echo "WARNING: source $SOURCE is ${DIMS} (not square)." >&2
  echo "         icon will be force-scaled to 1024x1024 which distorts the image." >&2
  echo "         Please prepare a square (1024x1024 or larger) source." >&2
fi

echo "==> Rasterising $SOURCE_KIND source ($DIMS) to PNGs"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

ICONSET="$WORK/icon.iconset"
mkdir -p "$ICONSET"

# まず 1024x1024 のマスターを作って、そこから縮小していく。
# sips は SVG / PNG どちらも入力に取れる。
MASTER="$WORK/master.png"
sips -s format png -z 1024 1024 "$SOURCE" --out "$MASTER" >/dev/null

# iconutil の命名規則に合わせて 10 ファイル生成。
declare -a SIZES=(
  "16   icon_16x16.png"
  "32   icon_16x16@2x.png"
  "32   icon_32x32.png"
  "64   icon_32x32@2x.png"
  "128  icon_128x128.png"
  "256  icon_128x128@2x.png"
  "256  icon_256x256.png"
  "512  icon_256x256@2x.png"
  "512  icon_512x512.png"
  "1024 icon_512x512@2x.png"
)
for entry in "${SIZES[@]}"; do
  read -r size name <<< "$entry"
  sips -z "$size" "$size" "$MASTER" --out "$ICONSET/$name" >/dev/null
done

echo "==> Generating icon.icns"
iconutil -c icns "$ICONSET" -o "$OUT_DIR/icon.icns"

echo
echo "Done (macOS .icns)."
echo "Generated files in $OUT_DIR/:"
ls -la "$OUT_DIR" | grep -E 'icon\.icns' || true
echo
echo "NOTE: Windows (.ico) / Linux (icon-256/512/1024.png) は"
echo "      tools/make_transparent_icon.py で別途生成すること。"
