#!/usr/bin/env python3
# images/icon-no-shadow.png (白背景 + 黒い複葉機シルエット) から、
# Windows / Linux 配布用の「背景なし (透過) + 白フチ (ステッカー風アウトライン)」
# アイコンを生成する。
#
#   出力:
#     images/icon-outlined.png   透過 + 白フチの 1024px マスター (中間生成物)
#     images/icon.ico            Windows 用 (16/24/32/48/64/128/256 を内包)
#     images/icon-256.png        Linux 用 (/usr/share/icons/.../256x256)
#     images/icon-512.png        Linux 用 (汎用)
#     images/icon-1024.png       Linux 用 (汎用)
#
# macOS の icon.icns は別系統 (Apple HIG の白角丸版)。これは
# tools/generate_icon_assets.sh が images/icon.png から生成するので、
# 本スクリプトでは一切触らない。
#
# 設計メモ:
#   - 元絵は「白地に黒インク」の 2 値的な線画なので、輝度を α に写して
#     白 → 透明 / 黒インク → 不透明 に抜く (INK_L / WHITE_L の間だけ AA)。
#   - 透過した黒シルエットはダークテーマの背景に溶けて見えなくなるため、
#     シルエットを円板状にディレート (全方位オフセットの max 合成) して
#     白いフチを後ろに敷く。明背景・暗背景どちらでも視認できる。
#
# 依存: Pillow (pip install Pillow)。アイコン素材更新時に手で実行し、
#       生成物をリポジトリにコミットすること (CMake からは自動実行しない)。

import math
import os
from PIL import Image, ImageChops, ImageFilter

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG = os.path.join(REPO, "images")
SRC = os.path.join(IMG, "icon-no-shadow.png")

INK = (30, 28, 24)   # シルエットのインク色 (元絵のほぼ黒に合わせる)
INK_L = 60           # 輝度 L <= INK_L  は完全不透明
WHITE_L = 240        # 輝度 L >= WHITE_L は完全透明 (この間だけ AA)
OUTLINE = 40         # 白フチの太さ (1024px 空間での半径)


def extract_silhouette(src):
    """白背景を抜いて、透過の黒シルエット RGBA を返す。"""
    src = src.convert("RGBA")
    w, h = src.size
    px = src.load()
    out = Image.new("RGBA", (w, h))
    op = out.load()
    span = WHITE_L - INK_L
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 255:  # 既存の透過は白でフラット化してから判定
                r = r + (255 - r) * (255 - a) // 255
                g = g + (255 - g) * (255 - a) // 255
                b = b + (255 - b) * (255 - a) // 255
            lum = (299 * r + 587 * g + 114 * b) // 1000
            if lum <= INK_L:
                na = 255
            elif lum >= WHITE_L:
                na = 0
            else:
                na = round(255 * (WHITE_L - lum) / span)
            op[x, y] = (INK[0], INK[1], INK[2], na) if na else (0, 0, 0, 0)
    return out


def dilate(mask, r, steps=64):
    """マスク (mode L) を半径 r の円板でディレートする。"""
    out = mask.copy()
    for i in range(steps):
        ang = 2 * math.pi * i / steps
        dx = round(r * math.cos(ang))
        dy = round(r * math.sin(ang))
        out = ImageChops.lighter(out, ImageChops.offset(mask, dx, dy))
    return out


def add_white_outline(sil, radius):
    w, h = sil.size
    halo = dilate(sil.getchannel("A"), radius).filter(ImageFilter.GaussianBlur(0.6))
    white = Image.new("RGBA", (w, h), (255, 255, 255, 0))
    white.putalpha(halo)
    res = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    res.alpha_composite(white)  # 白フチを後ろに
    res.alpha_composite(sil)    # 黒シルエットを前に
    return res


def main():
    if not os.path.exists(SRC):
        raise SystemExit(f"ERROR: {SRC} not found")
    print(f"==> Extracting silhouette from {os.path.relpath(SRC, REPO)}")
    sil = extract_silhouette(Image.open(SRC))
    print(f"==> Adding white outline (radius={OUTLINE})")
    master = add_white_outline(sil, OUTLINE)

    out_master = os.path.join(IMG, "icon-outlined.png")
    master.save(out_master)
    print(f"    wrote {os.path.relpath(out_master, REPO)} ({master.size[0]}x{master.size[1]})")

    print("==> Generating Linux PNGs (transparent)")
    for sz in (256, 512, 1024):
        p = os.path.join(IMG, f"icon-{sz}.png")
        master.resize((sz, sz), Image.LANCZOS).save(p)
        print(f"    wrote {os.path.relpath(p, REPO)}")

    print("==> Generating Windows icon.ico (transparent, multi-size)")
    ico = os.path.join(IMG, "icon.ico")
    sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    master.resize((256, 256), Image.LANCZOS).save(ico, format="ICO", sizes=sizes)
    print(f"    wrote {os.path.relpath(ico, REPO)}")

    print("\nDone. macOS icon.icns は変更していない (白角丸版を維持)。")


if __name__ == "__main__":
    main()
