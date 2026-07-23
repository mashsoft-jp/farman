# CLAUDE.md

このファイルは、Claude Code (claude.ai/code) がこのリポジトリで作業する際のガイダンスを提供します。

## プロジェクト概要

Farman は Qt6 ベースの C++ アプリケーションです。ビルドシステムには CMake を使用し、C++20 標準に準拠しています。Qt は macOS の Homebrew 経由で `/opt/homebrew/opt/qt` にインストールされています。

## ビルドシステム

### 前提条件
- CMake 3.21 以上
- Qt6 (Core および Widgets モジュール)
- C++20 対応コンパイラ

### プロジェクトのビルド方法

設定とビルド:
```bash
cmake -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build
```

アプリケーションの実行:
```bash
# GUI 起動 (Terminal から切り離される)
open ./build/farman.app

# Terminal にデバッグログを流したい開発時はバンドル内の実行ファイルを直接叩く
./build/farman.app/Contents/MacOS/farman
```

Windows では `build/farman.exe` (`WIN32_EXECUTABLE` により黒コンソール無しの GUI exe)、
Linux では `build/farman` がそのまま生成される。

クリーンビルド:
```bash
rm -rf build
cmake -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build
```

## コードアーキテクチャ

詳細は `ARCHITECTURE.md` を参照。仕様は `SPEC.md` を参照。

### アーキテクチャ原則
- **レイヤー分離**: UI / ビジネスロジック / モデル / データアクセス層を分離
- **Model-View パターン**: Qt の Model/View フレームワークを活用
- **非同期処理**: ファイル操作は QThread でバックグラウンド実行
- **プラグインアーキテクチャ**: ビュアーは動的ロード可能なプラグインとして実装

### ディレクトリ構造
```
src/
├── main.cpp
├── ui/          # UI層 (MainWindow, FilePane, etc.)
├── models/      # モデル層 (FileSystemModel, FilterSortProxy)
├── operations/  # ファイル操作 (Copy, Move, Delete)
├── viewers/     # ビュアーシステム (プラグイン対応)
├── settings/    # 設定管理 (Settings, KeyBindings, ColorScheme)
├── core/        # コア機能 (Bookmark, History, Search)
└── utils/       # ユーティリティ
```

### 主要設計パターン
- **Singleton**: `ViewerManager::instance()`, `Settings::instance()`
- **Observer**: Qt Signal/Slot によるイベント通知
- **Strategy**: `FileOperation` 派生クラス (Copy/Move/Delete)
- **Proxy**: `FilterSortProxy` によるフィルタ・ソート機能
- **Plugin**: `IViewerPlugin` インターフェースによる拡張機能

### CMake 設定
- Qt の適切な統合のために `qt_add_executable()` を使用
- CMake の AUTOMOC、AUTORCC、AUTOUIC を有効化し、Qt のメタオブジェクト、リソース、UI の自動コンパイルを実現
- Qt6::Core および Qt6::Widgets にリンク

## 開発ノート

### コードスタイル
- インデント: スペース 2 つ (タブ不使用)
- 文字エンコーディング: UTF-8
- 言語: C++20 標準を強制

### Qt 統合
- AUTOMOC が有効化されており、Q_OBJECT マクロを含むファイルで moc が自動実行される
- AUTORCC が .qrc リソースファイルを自動処理
- AUTOUIC が .ui フォームファイルを自動処理

### Claude Code への指示
- **日本語で出力してください**: このプロジェクトでは、Claude Code からの全ての出力を日本語で行ってください。

## ブランチ運用

- `main` は常にリリース可能な安定ブランチとして扱い、直接コードを書かない。
- `develop` / `staging` は使わない。
- 次リリースに入れる変更をまとめる場合は `release/vX.Y.Z` を作る。
- 個別作業は `feature/<topic>` または `fix/<topic>` で行い、確認後に対象の
  `release/vX.Y.Z` へ取り込む。
- 公開済み版への緊急修正だけ `hotfix/<topic>` を使う。必要に応じて対象タグ
  または `main` から切る。
- 公開済みバージョンは `vX.Y.Z` タグで固定する。正式リリース後は
  `release/vX.Y.Z` を `main` にマージし、main の HEAD に正式タグを打つ。

## リリースフロー

タグ命名規約:
- `vX.Y.Z-test`: テスト用のプレリリースビルド。CI で 3 OS のアセットを作るが Draft Release として公開され、ユーザー側でインストールして動作確認するためのもの。`release.yml` が tag push をトリガに走り、CMakeLists.txt の VERSION とタグサフィックスから FARMAN_VERSION (例: `0.9.2-test`) を組み立てる。
- `vX.Y.Z`: 正式リリース。同じく `release.yml` が走って 3 OS のアセットを Draft 公開する。

リリースする手順 (常にこの順序):
1. 個別作業ブランチ (`feature/...` / `fix/...`) で実装・コミットし、対象の
   `release/vX.Y.Z` に取り込む。CMakeLists.txt の VERSION も bump 済にしておく。
   `resources/whatsnew/whatsnew_{ja,en}.md` (アップデート内容ダイアログの文言)
   もこのリリースの内容に更新する。見出しのバージョンが VERSION と一致しないと
   release.yml の Pre-release checks (`tools/check_whatsnew.sh`) がビルド前に
   fail する。
   さらに **Web サイトの新機能ページも更新する**: `docs/whatsnew/index.html` と
   `docs/en/whatsnew/index.html` に新バージョンの節 (`.doc-section`、最新が上) を
   追加し、見出しにリリース日を併記する。トップページ (`docs/index.html` /
   `docs/en/index.html`) のヒーロー直下にある概要カード (`.wn-summary`) の
   見出し・日付・主要項目も新バージョンに差し替える。内容はアップデート内容
   ダイアログ (whatsnew) と揃える。docs/ は main へのマージ (手順 5) で GitHub
   Pages に自動デプロイされる。
2. **`vX.Y.Z-test` タグを `release/vX.Y.Z` の先端に直接打って push**。
   - main にはまだマージしない。テストで問題が見つかって release ブランチに修正を入れる場合、main を巻き戻す必要がない。
   - `git tag -a vX.Y.Z-test -m "..."; git push origin release/vX.Y.Z vX.Y.Z-test`
3. CI (release.yml) で 3 OS のビルドが通り、Draft Release ができたら macOS / Windows / Linux に配って動作確認する。
4. 問題が見つかったら `release/vX.Y.Z` に修正を追加コミット → `vX.Y.Z-test` を **force でリセット** (`git tag -f` + `git push --force origin vX.Y.Z-test`)。Draft Release が新しいビルドで上書き更新される。
5. テストが完了したら、はじめて **`release/vX.Y.Z` を main に fast-forward マージ** + push。
6. main の HEAD に `vX.Y.Z` 正式タグを打って push → 同じ `release.yml` が正式リリースの Draft を作る。
7. 正式リリースの Draft ができたら、**GitHub リリースページの本文に更新内容を
   記載する**。release.yml が自動生成するのは `**Full Changelog**` 行のみなので、
   その前に whatsnew と同じ内容を **日本語 → `---` → `# English` → 英語** の順
   (`##` 見出し) で追記する (v0.9.7 / v0.9.8 のリリース本文が手本。
   `gh release edit vX.Y.Z --notes-file <file>` で反映)。本文を確認してから手動で
   Publish。Publish された stable リリースを auto-update が検知する。

注意:
- `build.yml` は push trigger から `main` を外してある (PR は残してある)。main へのマージで CI が二重に走らないようになっている。`release.yml` のみがタグ push で走る。
- **force push** は v\*-test タグに限る運用にする。`vX.Y.Z` 正式タグや main ブランチへの force push はしない。
- 正式リリースを Publish したら、残っている `vX.Y.Z-test` の Draft Release とタグを
  削除して片付ける (`gh release delete vX.Y.Z-test -R mashsoft-jp/farman --cleanup-tag`)。
