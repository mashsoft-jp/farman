#!/usr/bin/env python3
"""shot.py <pid> <out.png> — farman の全ウィンドウを撮ってメインウィンドウ基準で 1 枚に合成する。
メインウィンドウ = 面積最大のウィンドウ。ダイアログ類は影を付けて重ねる。"""
import subprocess, sys, os, tempfile
S = os.path.dirname(os.path.abspath(__file__))
pid, out = sys.argv[1], sys.argv[2]
rows = []
for line in subprocess.run([S + '/out/bin/winid', pid], capture_output=True, text=True).stdout.splitlines():
    wid, _pid, layer, b, *_ = line.split('\t')
    x, y, w, h = [int(float(v)) for v in b.split(',')]
    if w < 8 or h < 8:
        continue
    rows.append((int(wid), x, y, w, h))
if not rows:
    sys.exit('no farman window')
main = max(rows, key=lambda r: r[3] * r[4])
tmp = tempfile.mkdtemp(prefix='shot')
# CGWindowList は手前 → 奥の順。奥から重ねる。
layers = [r for r in reversed(rows)]
cmd = ['magick', '-size', f'{main[3]}x{main[4]}', 'xc:white']
for r in layers:
    wid, x, y, w, h = r
    f = f'{tmp}/{wid}.png'
    subprocess.run(['screencapture', '-x', '-o', f'-l{wid}', f], check=True)
    ox, oy = x - main[1], y - main[2]
    if r is main:
        cmd += [f, '-geometry', f'+{ox}+{oy}', '-composite']
    else:
        # ダイアログ: 柔らかい影を敷いてから本体を重ねる
        cmd += ['(', f, '-background', 'black', '-shadow', '38x14+0+10', ')',
                '-geometry', f'+{ox - 28}+{oy - 18}', '-composite',
                f, '-geometry', f'+{ox}+{oy}', '-composite']
cmd += ['-alpha', 'remove', '-alpha', 'off', out]
subprocess.run(cmd, check=True)
print(out, f'{main[3]}x{main[4]}', f'windows={len(rows)}')
