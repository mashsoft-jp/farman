# ビルドとリリース

farman を**ソースからビルドする手順**と、**CI / リリースの運用**をまとめたドキュメント。
公開済みバイナリの入手やアプリの使い方は [README.md](README.md) を参照。

## ソースからのビルド

### macOS

前提パッケージ (Homebrew):

```bash
brew install qt libarchive uchardet pkg-config
```

ビルド:

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build
```

起動:

```bash
open ./build/farman.app
# 開発時にデバッグログを Terminal で見たいときはバンドル内の実行ファイルを直接:
./build/farman.app/Contents/MacOS/farman
```

配布用 `.dmg` の作成:

```bash
macdeployqt build/farman.app -dmg
# build/farman.dmg が生成される
```

### Linux (Debian / Ubuntu)

前提パッケージ:

```bash
sudo apt install -y \
  qt6-base-dev qt6-tools-dev qt6-5compat-dev \
  libarchive-dev libuchardet-dev \
  cmake pkg-config libgl1-mesa-dev libxkbcommon-dev
```

ビルド:

```bash
cmake -B build
cmake --build build
```

起動:

```bash
./build/farman
```

配布用 AppImage の作成 (linuxdeploy 利用):

```bash
# 初回のみ
wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
wget https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x linuxdeploy*.AppImage

# 生成
./linuxdeploy-x86_64.AppImage \
  --appdir AppDir \
  -e build/farman \
  -i images/icon-256.png \
  -d linux/farman.desktop \
  --plugin qt \
  --output appimage
```

### Windows

前提:

1. **Visual Studio 2022** をインストール (C++ によるデスクトップ開発ワークロード)
2. **Qt 6.10 以降** を [Qt Online Installer](https://www.qt.io/download-qt-installer-oss) からインストール
   - コンポーネント: **MSVC 2022 64-bit** + **Qt 5 Compatibility Module** にチェック
   - 例: `C:\Qt\6.10.3\msvc2022_64\`
3. **vcpkg** で libarchive / uchardet を取得:
   ```powershell
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   cd C:\vcpkg
   .\bootstrap-vcpkg.bat
   .\vcpkg integrate install
   .\vcpkg install libarchive:x64-windows uchardet:x64-windows
   ```

ビルド (Developer Command Prompt for VS 2022 から):

```powershell
cmake -B build ^
  -DCMAKE_PREFIX_PATH="C:\Qt\6.10.3\msvc2022_64" ^
  -DCMAKE_TOOLCHAIN_FILE="C:\vcpkg\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Release
```

起動:

```powershell
build\Release\farman.exe
```

配布用パッケージ (Qt DLL + 依存 DLL を実行ファイル横に同梱):

```powershell
windeployqt --release build\Release\farman.exe
copy C:\vcpkg\installed\x64-windows\bin\*.dll build\Release\
# build\Release\ ディレクトリ全体を zip して配布
```

### クリーンビルド (全プラットフォーム共通)

```bash
rm -rf build         # Windows: rmdir /s /q build
# 上記の cmake コマンドを再実行
```

## CI / リリース

- `.github/workflows/build.yml` — **3 OS × push 毎** の自動ビルド検証。
  成果物は Actions の artifact として 14 日保存される (動作確認用)。
- `.github/workflows/release.yml` — タグ push をトリガに 3 OS の配布
  パッケージ (DMG / AppImage + .deb / zip) をビルドし、GitHub Releases に
  **draft** として公開する。本人が GitHub UI で内容を確認してから
  "Publish release" を押すまで世に出ない運用。

### リリース手順

```bash
# 1. ローカルでバージョンタグを切る (vMAJOR.MINOR.PATCH 形式)
git tag v1.0.0
git push origin v1.0.0

# 2. GitHub の Actions タブで "Release" ワークフローの進行を確認
#    3 OS 並列ビルド → 30〜40 分程度

# 3. 完了後 Releases ページに draft が出来る
#    https://github.com/<owner>/farman/releases
#    - farman-v1.0.0-macos-arm64.dmg
#    - farman-v1.0.0-linux-x86_64.AppImage
#    - farman-v1.0.0-linux-x86_64.deb         (Debian / Ubuntu / Mint)
#    - farman-v1.0.0-windows-x64.zip

# 4. 動作確認 → "Edit" → "Publish release" で世に出る
```

事前テストしたい場合は `v0.0.0-test` のような prerelease タグで試すと
よい (`-` を含むタグは自動的に prerelease 扱い)。draft なので不要なら
削除して安全にやり直せる。タグも `git tag -d v0.0.0-test &&
git push --delete origin v0.0.0-test` で消せる。

**コード署名**: macOS は v0.9.5 から **Developer ID 署名 + 公証 (Notarization)
を CI で実装済み** (`MACOS_CERTIFICATE_BASE64` 等 5 つの Secret を repo に
設定するとフローが起動。詳細は SPEC.md「コード署名 (CI)」節)。`.app` /
`.dmg` とも `spctl` で "Notarized Developer ID" として受理される。
Windows Authenticode は未対応 (SmartScreen 注記で当面回避)。

リリースノートは `release.yml` が GitHub の auto-generated notes として
コミット / PR の差分を自動収集する。それとは別に、ユーザー視点の主要変更
点は [CHANGELOG.md](CHANGELOG.md) に Keep a Changelog 形式で記録している。

## 翻訳 (i18n)

`translations/farman_ja.ts` が日本語訳。Qt Linguist で開いて編集可能:

| OS | Linguist のパス例 |
|---|---|
| macOS | `/opt/homebrew/opt/qt/bin/Linguist` |
| Linux | `/usr/bin/linguist6` (apt 版) |
| Windows | `C:\Qt\6.10.3\msvc2022_64\bin\linguist.exe` |

`tr()` 文字列の抽出 (.ts への反映):

```bash
cmake --build build --target update_translations
```
