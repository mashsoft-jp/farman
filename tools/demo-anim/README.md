# トップページの操作デモ (アニメーション WebP) の撮影

Web サイトのトップページに置いている操作デモ
(`docs/screenshots/00-demo-ja.webp` / `00-demo-en.webp`) を撮り直すための道具。
タイトルバーにバージョンが出るので、リリースごとに撮り直す想定。macOS 専用。

## 仕組み

実時間の録画ではなく**コマ撮り**。キーを 1 手送るごとにウィンドウを撮影し、
コマごとの表示時間を付けてアニメーション WebP にまとめる。軽く (可逆圧縮で
約 300 KB)、マウスカーソルや通知が映り込まず、撮り直しが効く。

| ファイル | 役割 |
|---|---|
| `make-demo.sh` | デモ用データを `/tmp/farman-demo` に作り直す (更新日時は固定) |
| `mkprofile.sh` | 隔離した設定プロファイルを `out/home-<lang>` に作る |
| `record.py` | farman を隔離プロファイルで起動し、シナリオを実行してコマを撮る |
| `shot.py` | farman の全ウィンドウを撮り、メインウィンドウ基準で 1 枚に合成する |
| `encode.sh` | コマと表示時間から `img2webp` でアニメーション WebP を作る |
| `fkey.swift` | farman のプロセスへキーイベントを直接送る |
| `winid.swift` | farman のウィンドウ ID と位置を列挙する |
| `axwin.swift` | ダイアログの位置とサイズを整える (検索ダイアログが縦に長いため) |

`record.py` は `CFFIXED_USER_HOME` で設定の場所を差し替えて起動するので、
普段使いの設定や起動中の farman には影響しない。キーはプロセス宛てに送るので、
撮影中に他のアプリを操作しても入力は混ざらない (ただし farman のウィンドウは
前面に出る)。

## 前提

- `build/farman.app` がビルド済みであること (`FARMAN_BIN` で別のバイナリも指定可)
- Homebrew の `imagemagick` と `webp` (`magick` / `img2webp`)
- Xcode Command Line Tools (`swiftc`)
- 実行するターミナル (または Claude) に、システム設定 → プライバシーとセキュリティの
  「画面収録」と「アクセシビリティ」の許可があること
- 1 倍解像度のディスプレイで撮ること (Retina だと 2400x1400 になる。その場合は
  `encode.sh` の前に `magick mogrify -resize 50%` で縮小する)

## 手順

```bash
tools/demo-anim/build.sh                      # Swift ヘルパをビルド (初回のみ)
tools/demo-anim/record.py ja                  # 日本語 UI で撮影 (約 1 分半)
tools/demo-anim/record.py en                  # 英語 UI で撮影
tools/demo-anim/encode.sh ja docs/screenshots/00-demo-ja.webp -lossless -min_size
tools/demo-anim/encode.sh en docs/screenshots/00-demo-en.webp -lossless -min_size
magick tools/demo-anim/out/frames-en/001.png -strip docs/screenshots/00-demo-en.png
```

最後の 1 行は、英語ページで「視差効果を減らす」設定のときに出す静止画。
撮ったコマは `out/frames-<lang>/` に残るので、`magick montage` などで一覧にして
目視確認してからエンコードするとよい。

## シナリオを変えるとき

`record.py` の `step([...キー...], wait=秒, dur=表示ミリ秒, label=...)` の並びが
シナリオ。`dur` を付けた step だけがコマになる。注意点:

- コピー / 移動 / 削除は、確認ダイアログの後に**完了ダイアログ**が出る。閉じる
  Return を 1 つ忘れると、以降のキーがすべてずれる。
- 検索ダイアログはメインウィンドウより背が高いので、`fit_dialog()` で収めてから撮る。
- ウィンドウの高さはタイトルバー込みで 700 になるよう、`mkprofile.sh` で
  クライアント領域を 668 にしている。
