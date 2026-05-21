# Changelog

All notable changes to **farman** are documented in this file.

形式は [Keep a Changelog](https://keepachangelog.com/ja/1.1.0/)、
バージョンは [Semantic Versioning](https://semver.org/lang/ja/) に従う。

リリース毎のコミット / PR 単位の詳細は [GitHub Releases](https://github.com/ms-haraki/farman/releases)
を参照 (`release.yml` の `generate_release_notes` が前回タグからの差分を
自動でリリースノートにまとめる)。本ファイルは「ユーザーから見える主な
変更点」だけを要約する役割を担う。

## [Unreleased]

最初の正式リリース **v1.0.0** に向けて開発中。

### Added

- **UI / 操作**
  - 2 ペイン / シングルペインの UI 切替、ペイン間の同期ブラウズ
  - キーボード駆動の主要操作 (`c`/`m`/`k`/`d`/`r`/`n`/`a`/`f`/`p`/`u` 等)
  - 任意のキーバインドを完全カスタマイズ可能 + プリセット
- **ファイル操作**
  - コピー / 移動 / 削除 (ゴミ箱 or 完全削除) / リネーム / 一括リネーム
    (テンプレート + 連番 + 正規表現置換 + プレビュー)
  - 進捗ダイアログ / キャンセル対応 / 上書きモード (Ask / 自動上書き / 自動リネーム)
- **アーカイブ**
  - 作成・展開 (zip / tar / tar.gz / tar.bz2 / tar.xz)
  - 暗号化 zip の展開 (パスワード入力 + 検証付き)
  - **アーカイブ内ブラウジング**: 仮想 FS (`archive.zip!/inner/`) として閲覧、
    選択ファイルだけを反対ペインへ抽出コピー
- **検索 / ナビゲーション**
  - バックグラウンド再帰検索、結果からの直接ジャンプ
  - ブックマーク / ディレクトリ履歴 (永続化)
  - アドレスバー + パス補完
- **ディレクトリ比較**
  - 左右ペインの差分を行ごとに色分け表示
  - 同期ブラウズと併用可能、コピー後も比較モード維持
- **ビュアー (組み込み)**
  - テキスト (エンコード自動判定 / 行番号 / ワードラップ)
  - 画像 (ズーム / Fit to Window / 透明背景 Checker / GIF・WebP アニメーション)
  - バイナリ (16 進ダンプ + アドレス列・文字列列カラーリング)
  - 任意ビュアーで開く (Ctrl+Enter) / OS 既定アプリで実行 (Shift+Enter)
- **サムネイル表示**
  - ファイル一覧をリスト表示 / 小・中・大サムネイルの 4 段階で切り替え
    (Finder ライクに `Cmd/Ctrl+1〜4`、または View メニュー / ツールバー
    ポップアップから)
  - 画像 (svg / webp 含む) と PDF (1 ページ目) に対応
  - 非同期ワーカー + LRU キャッシュ (サイズ別の世代カウンタで取り違え防止)、
    アーカイブ内画像にも対応 (仮想 FS パスから直接サムネイル生成)
- **プレビューモード (Quick View)**
  - Single / Dual と並ぶ 3 つ目のレイアウト。左にファイル一覧、右に
    ビュアーを並べ、カーソル移動で右ペインの内容が逐次切り替わる
  - 切替: `Cmd/Ctrl+P` / View メニュー / ツールバーの Preview ボタン
  - ロード戦略: デバウンス 200ms + QtConcurrent ワーカー + 世代カウンタ +
    協調キャンセル (TextView / BinaryView)。読み込み中にカーソルが動いたら
    即座に次のファイルへ
  - 上限を超えるファイル (既定 10 MB) はプレビューせず "Too large" 表示。
    プレビューサイズ / デバウンス時間は Settings から変更可能
  - レイアウトは永続化、Splitter のサイズは Dual / Preview で独立記憶
- **自動アップデート**
  - 起動時に最大 1 日 1 回、GitHub Releases の最新タグをチェック
  - 新バージョンを検出したら通知ダイアログを表示 (リリースノートを
    Markdown でレンダリング)。`Update Now` / `Remind Me Later` /
    `Skip This Version` の 3 択
  - `Update Now` で OS 別アセット (macOS DMG / Windows setup.exe /
    Linux AppImage) をダウンロードし、同梱の SHA256 で検証してから
    インストーラを起動。`silent` モードでは確認なしで自動適用
  - Settings → 全般 タブから「起動時自動チェック」「サイレント更新」
    「今すぐチェック」を制御可能。最終チェック日時を表示
  - 開発ビルド (`0.0.0` / `0.0.0-dev`) では起動時の自動チェックを
    スキップ (手動チェックは可能)
- **外観 / カスタマイズ**
  - ライト / ダークテーマ、テーマプリセット
  - アドレスバー・カーソル・カテゴリ別ファイル色・行高 の細かい外観設定
- **その他**
  - ログペイン (日次ローテーション + 保持日数設定)
  - 国際化 (英語 / 日本語、Auto は OS 設定追従)
  - 外部変更の自動反映 (QFileSystemWatcher + デバウンス)
  - ステータスバーにアクティブペインのボリューム使用量表示
    (`N GB free / M GB (P% used)`、5 秒ポーリングで追従)。クラウド同期
    フォルダ (Google Drive / iCloud / OneDrive / Dropbox) を検出した場合は
    `<cloud sync folder>` 表示で容量抑止 (ホスト FS 容量が誤解を招くため)
- **CI / 配布**
  - 3 OS の自動ビルド (`build.yml`、macOS arm64 / Linux x86_64 / Windows x86_64)
  - タグ push → GitHub Releases 自動公開 (`release.yml`、draft 公開で安全運用)
  - Linux は **AppImage** に加えて **`.deb`** (Debian / Ubuntu / Mint 系)
    も同時配布。AppDir を `/opt/farman/` に詰める self-contained 方式で、
    OS 標準の Qt バージョン差に依存しない
  - `farman --version` / `--help` をコマンドラインで利用可能 (GUI を立ち上げず
    stdout 出力 + 即終了)
  - macOS DMG を典型 Mac インストーラレイアウトに整備 — 左に `/Applications`
    シンボリックリンク、右に `farman.app` を並べ、`create-dmg` でアイコン位置
    とウィンドウサイズを固定。ユーザーは Drag & Drop でインストール可能
  - Windows `.exe` インストーラ生成 (Inno Setup 6、`windows/farman.iss`)。
    スタートメニュー / デスクトップショートカット / アンインストーラを
    自動登録、Program Files / LocalAppData の両方の install 経路を
    サポート。ポータブル用 zip も併売

### Security

- アーカイブ展開時の **Zip Slip 攻撃**を多層防御で拒否:
  - `..` セグメント / 絶対パス / Windows backslash 経由の脱出をエントリ
    名段階・展開先パス組立段階・libarchive write_disk 段階で検査
  - libarchive の `ARCHIVE_EXTRACT_SECURE_SYMLINKS` を有効化
  - 出力ディレクトリの上位 symlink (macOS `/tmp` → `/private/tmp` 等) を
    `QFileInfo::canonicalFilePath()` で実体に解決してから libarchive に渡す
- ディレクトリのコピー / 移動先がコピー元自身 / 配下のとき拒否 (canonical
  path 比較による再帰展開バグの防止)
- 壊れたアーカイブの部分読み込みを「正常」扱いせず、致命エラーを通知して
  停止する

[Unreleased]: https://github.com/ms-haraki/farman/commits/main
