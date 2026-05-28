# Changelog

All notable changes to **farman** are documented in this file.

形式は [Keep a Changelog](https://keepachangelog.com/ja/1.1.0/)、
バージョンは [Semantic Versioning](https://semver.org/lang/ja/) に従う。

リリース毎のコミット / PR 単位の詳細は [GitHub Releases](https://github.com/mashsoft-jp/farman/releases)
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
  - **Markdown** (CommonMark + GitHub Flavored Markdown を `QTextDocument::setMarkdown`
    で整形表示。表 / タスクリスト / 取り消し線 / 自動リンク / 相対パス画像対応。
    ツールバー「Source」トグルで生 Markdown ソース表示に切替可)
  - **PDF** (Qt PDF / PDFium で組み込み表示。ページ送り / ページジャンプ /
    ズーム / 幅にフィット / ページ全体 / 連続表示トグル / 全文検索。
    Inline / External 両対応)
  - **CSV / TSV** (`QTableView` で表形式表示。RFC 4180 quoted-field パース、
    区切り自動判定 + 手動切替、エンコーディング自動判定、1 行目をヘッダ扱い
    トグル、セル内全文検索、巨大ファイル向けの遅延ロード (行オフセット index +
    LRU キャッシュで、12 MB / 20 万行クラスでも初回表示が即時)。
    Inline / External 両対応)
  - 任意ビュアーで開く (Ctrl+Enter) / OS 既定アプリで実行 (Shift+Enter)
  - **ビュアー読込のキャンセル + 結果ロギング**: Inline / External どちらの
    モードでも、ロード中に Esc 押下や別ファイル遷移でキャンセル可能
    (Cancel ボタン付きのモードレス進捗表示)。完了 / 失敗 / キャンセルは
    Logger に Info / Warn / Info 1 行で出力され、再現性のあるログが残る
- **画像ビュアーの強化**
  - ツールバーに **時計回り 90° 回転** ボタン (表示のみ、保存はしない)。
    角度ラベル表示、ファイル切替で 0° リセット、Fit-to-Window や External
    viewer のウィンドウフィットも回転後サイズに追従
  - **再生ボタンの自動有効化制御**: 1 フレームしかない GIF/WebP や静止画
    (PNG/JPEG/BMP) では disable、複数フレームのアニメだけ enable
  - **画像情報 ("i") ダイアログ** (モードレス、ファイル切替で内容自動更新):
    フォーマット / サイズ / 色深度 / DPI / 解像度
  - **JPEG / PNG (eXIf) / WebP (EXIF) / TIFF の Exif メタデータ**: 自前パーサで
    Camera Make/Model、撮影日時、絞り / ISO / 焦点距離 / フラッシュ /
    ホワイトバランス / 色空間、レンズ Make/Model、GPS 緯度経度高度 (海抜下
    対応)、QImageReader の埋込みテキストキー (PNG tEXt / iTXt / JPEG コメント) を
    表示。外部ライブラリ非依存
  - **ICC カラープロファイル名** ("sRGB IEC61966-2.1" / "Display P3" /
    "Adobe RGB (1998)" 等) を表示
  - **BMP** 対応 (24bit / 1bit / RLE)
  - **PSD (Adobe Photoshop) 対応**: 自前パーサで合成済プレビュー画像 (= Photoshop
    の "Maximize Compatibility" ON で保存される全レイヤマージ済 RGBA) を表示。
    Qt 同梱プラグインは PSD を扱えないため、`QImage::load()` 失敗時の fallback
    として動く。Image Info ダイアログも、`QImageReader` で取れないフォーマット
    は実 `QImage` から size / depth を取り出す経路を追加
- **ユーザー定義コマンドの並び替え** (External Apps タブ): 各コマンド行に ↑/↓
  ボタンを追加。Tools メニューの並び順 + タブの表示順がこの順序に従う
- **設定全体のエクスポート / インポート** (Settings → General → バックアップ /
  復元): ブックマーク / カスタムコマンド / カラースキーム / ビュアー設定 / パス
  まで含めた `settings.json` 全部を 1 ファイルで他マシンへ移行可能
- **プレビューペインのディレクトリ再帰サイズ集計**: ディレクトリにカーソルを
  当てると、件数の右に「12.3 MB (集計中…)」が出てバックグラウンド走査が進むに
  連れて更新、完了で「12.3 MB (M ファイル, K フォルダ)」に確定。カーソル移動で
  即キャンセル。Google Drive のような遅い FS でも安全に中断できる
  (走行中スレッドの破棄で qFatal するクラッシュを避けるため、cancel 後は
  `QThread::finished → deleteLater` の標準イディオムで自己破棄させる)
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
  - 上限超え (既定 10 MB) のテキスト / バイナリは**先頭 N バイトだけ読み込んで
    truncate 注記付きで表示**。画像は部分デコードできないので "Too large"
    メッセージ表示
  - プレビューサイズ / デバウンス時間は Settings から変更可能
  - **アーカイブ内エントリも対応**: zip / tar 等を開いた状態でカーソル移動
    すると、エントリを一時展開して通常ビュアー経路でプレビュー
  - ディレクトリは Finder Quick Look 風 (フォルダアイコン + パス + "N items")
  - レイアウトは永続化、Splitter のサイズは Dual / Preview で独立記憶
  - **Tab でプレビューにフォーカス進入**: ファイルリストペインのローカル
    Tab 連鎖 (★ → アドレスバー → 📁 → リスト → モード切替) の末尾で
    `Tab` を押すとプレビュー側のツールバー / メインコンテンツへ進入。
    プレビュー側末尾で `Tab` を押すと自ペイン ★ に戻り、Tab 連鎖が
    閉ループになる。`Shift+Tab` は逆方向。`Esc` で即座にファイルリストへ復帰
- **Tab フォーカス連鎖をレイアウト跨ぎで一本化**: Dual モードでは
  対向 FileListPane の頭 / 末尾、Preview モードでは PreviewPane の
  先頭 / 末尾、Single モードでは同ペイン内ラップ。どこから Tab を
  押し始めても予測可能な順序で全 UI 要素を巡回できる
- **非アクティブ時のテーブル行ハイライトを薄く**: `QTableView` /
  `QListWidget` 等の派生ウィジェットは Qt 既定だとフォーカスを失っても
  選択行を `palette(highlight)` (青) で強調し続け、「今フォーカスが
  あるのか?」が分かりにくい。NSTableView 風に **非アクティブ時はグレーに
  落とす** 共通スタイルを Settings / Keybindings ダイアログ等のリスト系
  ウィジェットへ一括適用
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

[Unreleased]: https://github.com/mashsoft-jp/farman/commits/main
