#!/usr/bin/env python3
"""record.py <ja|en> — デモデータを作り直し、隔離プロファイルで farman を起動して
シナリオを実行、out/frames-<lang>/NNN.png と durations.txt を出力する。
使い方と前提は README.md を参照。"""
import subprocess, sys, os, time, shutil
S = os.path.dirname(os.path.abspath(__file__))
lang = sys.argv[1]
REPO = os.path.abspath(os.path.join(S, '..', '..'))
BIN = os.environ.get('FARMAN_BIN', REPO + '/build/farman.app/Contents/MacOS/farman')
out = f'{S}/out/frames-{lang}'
shutil.rmtree(out, ignore_errors=True); os.makedirs(out)

subprocess.run([f'{S}/make-demo.sh'], check=True, stdout=subprocess.DEVNULL)
home = subprocess.run([f'{S}/mkprofile.sh', lang], check=True, capture_output=True, text=True).stdout.strip()
env = dict(os.environ, CFFIXED_USER_HOME=home)
proc = subprocess.Popen([BIN], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
pid = str(proc.pid)
time.sleep(3.0)

def keys(*toks):
    subprocess.run([f'{S}/out/bin/fkey', pid, *toks], check=True)

frames = []
def shot(dur_ms, label=''):
    n = len(frames) + 1
    f = f'{out}/{n:03d}.png'
    subprocess.run([f'{S}/shot.py', pid, f], check=True, stdout=subprocess.DEVNULL)
    frames.append((f, dur_ms, label))
    print(f'{n:03d} {dur_ms:5d}ms {label}')

def step(toks, wait=0.45, dur=None, label=''):
    keys(*toks)
    time.sleep(wait)
    if dur is not None:
        shot(dur, label)

def fit_dialog(w, h):
    """フォーカス中のダイアログをメインウィンドウ中央に w x h で収める"""
    mx, my, mw, mh = 120, 90, 1200, 700
    subprocess.run([f'{S}/out/bin/axwin', pid, 'set', str(mx + (mw - w)//2), str(my + (mh - h)//2 + 8), str(w), str(h)], check=True)
    time.sleep(0.4)

keys('activate')
time.sleep(0.8)

# ── 0. イントロ ───────────────────────────────
shot(1600, 'intro: left root')
step(['down'], dur=350); step(['down'], dur=350); step(['down'], dur=600, label='cursor on documents')
step(['return'], wait=0.9, dur=1000, label='enter documents')

# ── 1. コピー ────────────────────────────────
step(['down'], dur=350)
step(['space'], dur=400); step(['space'], dur=400); step(['space'], dur=800, label='3 files selected')
step(['c'], wait=0.9, dur=2300, label='copy confirm dialog')
step(['return'], wait=1.2, dur=1100, label='copy progress done')
step(['return'], wait=0.9, dur=1900, label='copied -> right pane')

# ── 2. 移動 ─────────────────────────────────
step(['space'], dur=400); step(['space'], dur=800, label='2 files selected')
step(['m'], wait=0.9, dur=2100, label='move confirm dialog')
step(['return'], wait=1.2, dur=1100, label='move progress done')
step(['return'], wait=0.9, dur=1900, label='moved')

# ── 3. 削除 ─────────────────────────────────
step(['d'], wait=0.9, dur=2100, label='delete confirm dialog')
step(['return'], wait=1.2, dur=1100, label='delete progress done')
step(['return'], wait=0.9, dur=1700, label='deleted')

# ── 4. 一括リネーム ───────────────────────────
step(['backspace'], wait=0.9, dur=800, label='back to root')
step(['down'], dur=500, label='cursor on images')
step(['return'], wait=0.9, dur=900, label='enter images')
step(['cmd+a'], dur=900, label='select all')
step(['cmd+r'], wait=0.9, dur=1500, label='bulk rename dialog')
step(['cmd+a', 'text:trip-{n3}.{ext}'], wait=0.7, dur=2600, label='template typed, preview')
step(['return'], wait=1.2, dur=1900, label='renamed')

# ── 5. 検索 ─────────────────────────────────
step(['backspace'], wait=0.9, dur=800, label='back to root')
keys('f'); time.sleep(0.9); fit_dialog(820, 656)
shot(1400, 'search dialog')
step(['text:*.md *.txt'], wait=0.5, dur=1100, label='pattern typed')
step(['return'], wait=1.3, dur=2300, label='search results')
step(['shift+tab', 'shift+tab', 'shift+tab', 'shift+tab'], wait=0.5, dur=1100, label='result row focused')
step(['return'], wait=1.0, dur=2600, label='jumped to result')

with open(f'{out}/durations.txt', 'w') as fp:
    for f, d, l in frames:
        fp.write(f'{os.path.basename(f)}\t{d}\t{l}\n')
print('total', sum(d for _, d, _ in frames) / 1000, 's', len(frames), 'frames')

keys('cmd+q'); time.sleep(1.0)
if proc.poll() is None:
    proc.terminate()
