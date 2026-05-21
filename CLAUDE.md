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

## リリースフロー

タグ命名規約:
- `vX.Y.Z-test`: テスト用のプレリリースビルド。CI で 3 OS のアセットを作るが Draft Release として公開され、ユーザー側でインストールして動作確認するためのもの。`release.yml` が tag push をトリガに走り、CMakeLists.txt の VERSION とタグサフィックスから FARMAN_VERSION (例: `0.9.2-test`) を組み立てる。
- `vX.Y.Z`: 正式リリース。同じく `release.yml` が走って 3 OS のアセットを Draft 公開する。

リリースする手順 (常にこの順序):
1. 作業ブランチ (claude/...) で機能実装・コミット。CMakeLists.txt の VERSION も bump 済にしておく。
2. **`vX.Y.Z-test` タグを作業ブランチの先端に直接打って push**。
   - main にはまだマージしない。テストで問題が見つかってブランチに修正を入れる場合、main を巻き戻す必要がない。
   - `git tag -a vX.Y.Z-test -m "..."; git push origin claude/<branch> vX.Y.Z-test`
3. CI (release.yml) で 3 OS のビルドが通り、Draft Release ができたら macOS / Windows / Linux に配って動作確認する。
4. 問題が見つかったら作業ブランチに修正を追加コミット → `vX.Y.Z-test` を **force でリセット** (`git tag -f` + `git push --force origin vX.Y.Z-test`)。Draft Release が新しいビルドで上書き更新される。
5. テストが完了したら、はじめて **作業ブランチを main に fast-forward マージ** + push。
6. main の HEAD に `vX.Y.Z` 正式タグを打って push → 同じ `release.yml` が正式リリースの Draft を作る。
7. Draft Release を確認してから手動で Publish。Publish された stable リリースを auto-update が検知する。

注意:
- `build.yml` は push trigger から `main` を外してある (PR は残してある)。main へのマージで CI が二重に走らないようになっている。`release.yml` のみがタグ push で走る。
- **force push** は v\*-test タグに限る運用にする。`vX.Y.Z` 正式タグや main ブランチへの force push はしない。
