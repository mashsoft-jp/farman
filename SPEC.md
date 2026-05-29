# farman アプリケーション仕様書

## 概要

Qt6 / C++20 製のクロスプラットフォーム2画面ファイラ。キーボード操作を主軸とし、
Total Commander / Double Commander に近い操作感を目指す。

Mashsoft Inc. が MIT ライセンスで公開する OSS。リポジトリは
**github.com/mashsoft-jp/farman** (会社 org 配下。旧 `ms-haraki/farman` から
2026-05 に Transfer 移管済み。旧 URL は GitHub のリダイレクトで追従)。

---

## 対応プラットフォーム

- macOS
- Windows
- Linux

---

## 画面構成

```
┌─────────────────────────────────────────────────────┐
│ メニューバー                                          │
├──────────────────────────┬──────────────────────────┤
│ 左パネル                  │ 右パネル                  │
│ /Users/masashi/          │ /Volumes/External/        │
│──────────────────────────│──────────────────────────│
│ ../                      │ ../                       │
│ Documents/               │ backup/                   │
│ Downloads/               │ photos/                   │
│ photo1.jpg    1.2MB ...  │ file1.txt    4KB  ...     │
│ ...                      │ ...                       │
│──────────────────────────│──────────────────────────│
│ Sort: ... │ Filter: ... │ Sort: ... │ Filter: ...  │ ← パネルフッター (sort/filter 状態)
├──────────────────────────┴──────────────────────────┤
│ ログ (Ctrl+L で表示切替, デフォルト ON)              │
│ [HH:mm:ss] [INFO] Copy done: 3 item(s) → /tmp       │
├─────────────────────────────────────────────────────┤
│ /path/to/file.txt          128 items  3 selected... │ ← ステータスバー
└─────────────────────────────────────────────────────┘
```

---

## パネル

### ファイル一覧の表示列

| 列 | 内容 | 既定 (2画面) | 既定 (1画面) | 備考 |
|---|---|---|---|---|
| 名前 | ファイル名・ディレクトリ名 | 常時 | 常時 | 非表示にできない |
| 種別 | 拡張子から判定した種別 | ON | ON | |
| サイズ | ファイルサイズ（ディレクトリは `<DIR>`） | ON | ON | |
| 更新日時 | 最終更新日時 | ON | ON | |
| 作成日時 | ファイル作成時刻 | OFF | ON | FS が birthTime をサポートしないと空表示 |
| パーミッション | `rwxr-xr-x` 形式 | OFF | ON | macOS / Linux のみ表示 (Windows は ACL なので非表示) |
| 属性 | `H` 隠し / `R` 読み取り専用 / `L` シンボリックリンク | OFF | OFF | Windows のみ表示 (Unix では Permissions と冗長なので非表示) |
| 所有者 | ユーザー名 (取得不可なら uid) | OFF | ON | macOS / Linux のみ表示 |
| グループ | グループ名 (取得不可なら gid) | OFF | ON | macOS / Linux のみ表示 |
| リンク先 | シンボリックリンクの解決先 (`→ /target`) | OFF | OFF | |

- 列の **表示／非表示** はディレクトリの **2 画面モード / 1 画面モード** ごとに別々に設定できる (Behavior タブ → List Display → Columns)
- 列の **並び順は固定** (上の表の順序)。ヘッダのドラッグ並べ替えは未対応

### 表示オプション

- 隠しファイルの表示・非表示
- ディレクトリを先頭に表示
- `.` / `..` の表示

### サムネイル表示モード

画像主体のディレクトリで便利な、リスト表示の代替モード。実装済み。

- 表示モードは **リスト / サムネイル (小) / サムネイル (中) / サムネイル (大)**
  の 4 段階。Finder 風に `Cmd/Ctrl+1〜4` で直接切替、もしくは View メニュー /
  ツールバーのモードポップアップから選択。
- サムネイルモード時の挙動:
  - 画像 (Settings の Image Viewer 拡張子に該当するもの。`svg` / `webp` 含む)
    と PDF (1 ページ目) はサムネイルを生成。
  - 非対応ファイルは従来通り種別アイコン (`QFileIconProvider`)。
- 実装:
  - サムネイル生成は **ワーカースレッド** で行い、メインスレッドは
    生成完了通知を受けてアイテムだけ再描画する (`ThumbnailCache`)。
  - `QImageReader::read(targetSize)` で縮小読み込み。LRU + 世代カウンタ
    で取り違えを防止。
  - 可視範囲のみ生成 (スクロールに応じてキューに追加 / キャンセル)。
  - 表示は `QListView::IconMode`。`QStackedWidget` でリスト⇔サムネイル
    を切替し、両方とも `FilterSortProxy` を共有するので選択状態は保持。
- アーカイブ内画像も対応 (仮想 FS パスを直接サムネイル化)。
- 動画のサムネイル化は OS / 外部依存が大きく、**v1.0 では見送り**
  (動画 / 音声ビュアー本体と合わせてプラグイン候補)。

### ソート

- ソートキー: 名前・サイズ・種別・更新日時
- 第2ソートキーも指定可能
- 昇順・降順
- 大文字小文字の区別
- ディレクトリの表示
  - ディレクトリをリスト先頭・最後尾・特になし
- ソート基準はディレクトリ単位で記憶

### フィルタ

- 名前マスク（glob: `*.jpg *.png` など任意の文字列で複数指定可）
- 属性フィルタ（通常ファイル・ディレクトリ・隠しファイル等）
- フィルタはディレクトリ単位で記憶

### 即時フィルタ (Quick Filter Bar)

ファイルリスト下部に **1 行のフィルタ入力欄** をトグル表示し、入力する
たびにリストを **その場で絞り込む** 機能。Total Commander のクイック
検索 (Ctrl+S) や Double Commander の Quick Filter と同様。

- ショートカット **`/`** (`view.quick_filter`) で入力欄をトグル ON/OFF。
  vim / less 系ツールの慣習に揃えた採用 (Total Commander の `Ctrl+S` は
  farman の `s` (`pane.sort_filter`) と相性が悪いので不採用)。View メニュー
  にも項目を置く。
- 入力中はファイルリストの行が「名前にその文字列を含むもの」だけに
  絞り込まれる (部分一致 / 大文字小文字区別なし)。`..` (親ディレクトリ)
  は常に表示されるので、絞り込み中でも親に戻れる。
- 既存の **Sort & Filter ダイアログ** や **`Shift+文字` 頭文字ジャンプ**
  とは別経路:
  - Sort & Filter は永続的なフィルタ条件 (ディレクトリに保存可能)。
  - 頭文字ジャンプは **カーソル移動だけ** (リストの絞り込みはしない)。
  - 即時フィルタは **その場限り** の表示絞り込み。
- バーの開閉 / 解除挙動:
  - **`/` (open)**: バー非表示時に押すと、直前のフィルタ文字列があればそれを
    バーに **復元 + 全選択** した状態で開く (打ち直し / 部分編集 / Enter で
    確定のいずれにも進めやすい)。フィルタが無いときは空のバーが出るだけ。
  - **`/` (close)**: バー表示時に押すと、フィルタとバーの内容を保ったまま
    バーだけ閉じてフォーカスをリストに戻す (= Enter と同じ動作)。
  - **`Enter`**: フィルタを保ったままバーを閉じる。バー内のテキストを
    空にしてから Enter を押せば、`textEdited` 経由でフィルタが解除済みなので
    自然に「フィルタ off」状態でバーだけ閉じる形になる。
  - **`Esc`**: フィルタとバー内容を **クリア** してバーを閉じる。
  - **ディレクトリ移動** (`setPath`) で自動的にクリアされる
    (新しいディレクトリに前のフィルタが残らない)。
- フッタの "Filter:" 行末尾に **「Quick: "<text>" (N / M items)」** の形式で
  絞り込み内容と件数を表示する (N = 表示中、M = 全件、いずれも `..` を除く)。
- 内部実装: `FileListModel` に「全件キャッシュ (`m_allEntries`)」と
  「フィルタ後の表示用 (`m_entries`)」を分離し、`applyFilterAndSort()` が
  非破壊で `m_entries` を作り直す形に変更。`m_liveFilter` (空文字 = 無効)
  を `passesFilters()` で評価し、setLiveFilter() で即反映する。
  `shared_ptr<FileItem>` を共有しているので、フィルタ切替で選択状態は失われない。

### パネル間のディレクトリ同期

アクティブパネル / 非アクティブパネルのカレントディレクトリを互いに反映できる。

- `pane.sync_other_to_active`: 非アクティブ側を、アクティブと同じディレクトリへ移動。
  デフォルトキー `Ctrl+Right`。
- `pane.sync_active_to_other`: アクティブ側を、非アクティブと同じディレクトリへ移動。
  デフォルトキー `Ctrl+Left`。
- View メニューにも同名のエントリを置く。

### 同期ブラウズ (Sync Browse)

トグル ON のとき、片方のペインでディレクトリを移動するともう一方の
ペインも **同じ相対変化** をその場の現在地から実行するモード。
Krusader / Total Commander の Sync Browse 相当。

- 切替手段:
  - **View メニュー**: チェック付きの "Sync Browse" 項目。
  - **キーバインド**: 既定 `y` (Settings → Keybindings で変更可)。
  - **ツールバー**: Single Pane / Sync Browse / Log の並びにトグル
    ボタンとして実装済。
- ON のときの視覚インジケータ:
  - メニューのチェックマーク
  - ステータスバー右側に "Sync Browse: ON" ラベル
  - ウィンドウタイトルにサフィックス "[Sync]"
- 動き方: ON にした瞬間に左右の起点を覚えるなどはせず、**ナビゲート毎に
  「片方のペインが oldPath → newPath で進んだ相対変化」を計算し、
  もう一方の現在地から同じ動きをさせる**。
  - 例: 左で `cd src` (`/old/farman` → `/old/farman/src`) すると、
    右が `/new/farman` にいれば `/new/farman/src` へ追従。
  - 例: 左で `cd ..` すると右も親方向へ移動。
  - 例: 絶対ジャンプ (アドレスバー編集や bookmark で全く別の場所へ
    飛ぶ) の場合、計算結果のターゲットがもう一方に存在しないことが
    多いので自然に no-op になる。
- 相対パスでの追従先が存在しない場合は **同期を自動解除** する
  (据え置きを続けると左右がズレたまま気付かない恐れがあるため、
   明示的に OFF にして状態をユーザーに知らせる)。ログにも記録。
- 用途: 同名ディレクトリ構造を持つ 2 つのツリー (例: 旧版と新版の
  ソース、バックアップ元と先) を並行に行き来する。
- ペイン切替・ファイル選択・コピー / 移動などの操作はトグル状態に
  影響されず通常通り動作する。
- **シングルペイン時の挙動**:
  - シングルペインに入った瞬間に Sync Browse は強制 OFF。
  - シングルペイン中はメニュー項目が disabled、`y` キーは no-op
    (ログに記録)。
  - 2 ペインに戻っても自動復元はしない (ユーザーが明示再 ON)。
- トグル状態は持ち越さない (起動時は OFF)。Settings に保存しない。

### プレビューモード (Quick View)

Total Commander の Ctrl+Q、macOS Finder の Cover Flow 相当。シングル /
デュアルと並ぶ 3 つ目のレイアウト。

- レイアウト: **左ペイン = ファイル一覧 / 右ペイン = ビュアー**。
- 切替方法:
  - `Ctrl+P` (デフォルト、`pane.toggle_preview`)
  - View メニュー → "Preview Pane"
  - ツールバーの Preview ボタン (Single Pane と排他チェック)
- カーソルが移動するたびに右ペインの内容が切り替わる。種別判定 (ViewerPanel::
  resolveAuto) でテキスト / 画像 / バイナリのどれかを表示。
- ロード戦略 (`src/ui/PreviewController`):
  - **デバウンス** (既定 200ms / Settings::previewDebounceMs で 50〜1000ms 可変)
    でカーソル連打を吸収。
  - **非同期ロード**: prepareLoad を QtConcurrent::run でワーカースレッドに
    投げる。結果到着時、内部の世代カウンタ (atomic) が一致していれば
    PreviewPane に流す、合わなければ捨てる (= 「読込中にカーソル移動 → 次へ」
    要件)。
  - **協調キャンセル**: TextView / BinaryView の prepareLoad は
    `std::atomic<bool>` のキャンセルトークンを受け取り、各ステージ間や
    重い hex 整形ループ (256 行ごと) でチェックして早期 return する。
    ImageView は QImageReader::read() の途中中断不可なので世代チェック
    だけに任せる。
  - **Loading 表示の遅延**: 150ms 経っても結果が返らないときだけ Loading
    page を表示 (小ファイルでチカチカさせない)。
- 上限超過時の挙動 (`Settings::previewMaxFileSizeBytes`、既定 10 MB):
  - **テキスト / バイナリ**: 先頭 N バイトだけ読み込んで表示し、末尾に
    `[truncated: showing first X of Y bytes]` 注記を付ける。
    BinaryView は元々 8 MB 内蔵上限を持っていたが、プレビュー経由なら
    Settings 値が優先される。
  - **画像**: QImageReader::read() は部分デコードできないため
    "File too large to preview" メッセージで拒否。プレビューせず通常の
    Enter 操作でビュアー起動を促す。
- 特殊なカーソル位置:
  - `..` 行: 空表示。
  - ディレクトリ: macOS Finder Quick Look 風のページ
    (大きめフォルダアイコン + パス + 浅い件数 "N items" + 再帰サイズ)。
    再帰サイズはバックグラウンドの `PropertiesWorker` で集計し、256 ファイル
    ごとの `statsUpdated` で「12.3 MB (集計中…)」と progressive 更新、完了で
    「12.3 MB (M ファイル, K フォルダ)」に確定する。カーソル移動 / プレビュー
    クリア時にワーカーは即 cancel される。
    - **ライフサイクル注意**: ワーカーは `parent=nullptr` で生成し、cancel 後は
      `QThread::finished → deleteLater` の標準イディオムで自己破棄させる。
      `deleteLater()` を即時呼ぶと、`lstat()` で長時間ブロックする FS
      (Google Drive のような cloud-streaming、ネットワーク FS、暗号化マウント
      など) で `~QThread()` が走行中スレッドを破棄しようとして qFatal する。
      PreviewController の dtor では現役 worker のみ `cancel + wait + delete`
      を同期実行する。
  - 非通常ファイル / 存在しないファイル: "Not a regular file." /
    "File not found." メッセージ。
- アーカイブ内エントリのプレビュー (実装済):
  - 左ペインがアーカイブ内ブラウジング (`archive.zip!/inner/`) になっている
    状態でカーソル移動すると、エントリを **`PreviewController` の
    `QTemporaryDir` に一時展開** してから通常の prepareLoad 経路に流す。
  - 展開先は `<archiveSha1[0..7]>/<safeJoin(entryPath)>` 形式の決定論的命名で、
    同じエントリへの再要求では再展開を行わない (サイズが一致すれば再利用)。
  - `safeJoinExtractPath` で Zip-Slip 攻撃を多層防御する (Enter で開く本来の
    ビュアー経路と同じ防御線)。
  - 一時ディレクトリは `PreviewController` 破棄時に丸ごと削除される。
  - 上限超過チェックは展開前のサイズ (= アーカイブの uncompressed size) で
    行うので、巨大エントリを展開してから捨てる無駄が発生しない。
- 制約:
  - プレビュー中は **Sync Browse / Directory Compare は強制 OFF** (右ペインが
    ビュアーなので意味を成さない)。
  - `←` は親ディレクトリへ、`→` は no-op (シングルペインと同じ挙動)。
  - `Tab` / `Shift+Tab` はプレビュー側に進入する (下記フォーカス節参照)。
  - コピー / 移動 / 抽出の既定宛先はアクティブペイン (= 左) を使う。
- フォーカス:
  - 初期フォーカスは常に左ペイン (アクティブペインは強制で Left に揃える)。
  - 左ペインで `Tab` を押すと **アクティブペインのローカル連鎖を最後まで
    辿った後 (= モード切替ボタンの次)**、プレビューペインの先頭 focusable
    (典型的にはツールバーの左端ボタン) へ進入する。`Shift+Tab` は逆方向で、
    左ペインの ★ から `Shift+Tab` するとプレビューペインの末尾 focusable
    (典型的にはメインコンテンツ) へ入る。
  - プレビュー内では各ビュアーのローカルチェーン (ツールバー → メイン
    content) を `Tab` / `Shift+Tab` で辿れる。末尾で `Tab` / 先頭で
    `Shift+Tab` を押すとプレビューを抜けて左ペインの ★ / モード切替ボタンに
    戻る (Tab 連鎖が閉ループになる)。
  - 状態ページ (Empty / Loading / Unsupported / Directory) では focusable
    な相手が居ないので、`Tab` での進入は自ペインの ★ / モードボタンに
    ラップする (= プレビュー側を素通り)。
  - プレビュー内で `Esc` を押すと、即座に左ペインのファイルリストへ
    フォーカスを戻す (テキスト選択 / コピー後の復帰用)。
- レイアウトは Settings (`behavior.layoutMode = "preview"`) に永続化されるため、
  プレビューモード中に終了すれば次回起動時もプレビューレイアウトで開く。
- Splitter のドラッグ位置は Dual / Preview それぞれ独立に記憶される。

### アクティブペインの切替

- **マウスクリック**: 非アクティブ側ペインのファイルリスト上で MouseButtonPress
  を受けたら、まずそのペインをアクティブにしてからクリック処理を続行する
  (カーソル移動なども 1 回のクリックで完了する)。
- **`←` / `→` キー**: アクティブパネル端で押すと反対側ペインへアクティブ切替。
  端でない場合は親ディレクトリへ移動。
- **アクティブ化時のスクロール補正**: マウスでスクロールしてカーソル
  (currentIndex) を画面外に追いやった状態のまま反対ペインへ離れて戻ったとき、
  `setActivePane()` から `scrollTo(EnsureVisible)` を呼び、カーソル行を
  最小限の移動で表示範囲に戻す。「カーソルが画面のどこにあるか分からない」
  状態を作らない。
- `Tab` / `Shift+Tab` は本来「フォーカス循環」用だが、Dual モードでアクティブ
  ペインのローカル連鎖の端 (mode + `Tab` または ★ + `Shift+Tab`) に達したとき
  だけは、結果として反対ペインへフォーカスが移る (= 連鎖の続きが対向側になる)。
  ペインの中央 (ファイルリスト本体) で `Tab` を押した場合は、まず自ペインの
  モード切替ボタンに進む — 反対ペインへは即座には飛ばない。
  詳細は「[Tab フォーカス連鎖](#tab-フォーカス連鎖)」節を参照。

### ディレクトリ単位のソート・フィルタ設定

現在開いているディレクトリに対するソート・フィルタ設定を、デフォルト設定と
独立して変更できる。

- **ショートカットキー**でダイアログを開き、そのディレクトリのソート・フィルタ
  を編集する（デフォルト: `s`、コマンド `pane.sort_filter`）。
- ダイアログで「このディレクトリのデフォルトを上書きする (保存)
  / Override defaults for this directory (save)」にチェックを付けて OK すると、
  そのパス専用の上書きとして永続化する。Behavior タブのデフォルトより
  優先され、このディレクトリを開いたときに自動で適用される。
- チェックを外して OK した場合は現在のパネルにのみ一時的に適用（保存しない）。
- 既に保存されているディレクトリでは、設定済みのチェック状態が初期値となる。
  チェックを外して OK すると保存済み上書きを削除し、Behavior タブの
  デフォルトに戻す。
- ディレクトリに移動した際、保存済み上書きがあれば自動で適用する。
  なければパネルの既定（Behavior タブのデフォルト）を適用する。

#### 保存形式

- 保存先は単一の設定ファイル（`settings.json`）に集約し、絶対パスをキーとする。
  理由:
  - 読み取り専用／ネットワーク／リムーバブルメディアにも対応可能
  - 共有フォルダや VCS 管理下のディレクトリを隠しファイルで汚染しない
  - 保存された設定の一覧・削除を一箇所で管理できる
- 設定ファイルの `pathOverrides` セクションにパスごとのソート・フィルタを格納。

---

## キーボード操作

以下はデフォルトキーアサイン。Keybindings タブで全て変更可能。
macOS の `Ctrl` は `⌘`（Command）に割り当てられる。

### 基本ナビゲーション

| キー | 動作 |
|---|---|
| `↑` / `↓` | カーソル移動 |
| `←` / `→` | アクティブパネルの端では親ディレクトリへ移動、逆側ならパネル切替（シングルパネルでは左のみ親へ） |
| `Home` / `End` | 先頭 / 末尾へジャンプ |
| `PageUp` / `PageDown` | ページ単位で移動 |
| `Shift+文字キー` | 押下文字を頭文字とする次のファイル／ディレクトリへカーソルを移動（末尾まで見つからなければ先頭へ循環）。連打で順次次のマッチへ進む。`Shift` を伴わない文字キーでは発動しない |
| `Enter` | ディレクトリに入る / ファイルをビュアーで開く |
| `Backspace` | 親ディレクトリへ移動 |
| `Tab` / `Shift+Tab` | フォーカス循環。アクティブパネル内は ★ → アドレスバー → 📁 → ファイルリスト → モード切替ボタン の順 (`Shift+Tab` は逆)。末尾 (mode + `Tab`) / 先頭 (★ + `Shift+Tab`) でレイアウトに応じて遷移: Dual = 反対ペインの先頭/末尾、Preview = プレビュー側の先頭/末尾、Single = 同ペイン内でラップ。詳細は「[Tab フォーカス連鎖](#tab-フォーカス連鎖)」節 |
| `Space` | カーソル行を選択・解除してカーソルを下に移動 |
| `Shift+Space` | カーソル行を選択・解除（カーソル移動なし） |
| `i` | 選択を反転 |
| `Ctrl+A` | 全選択 |

### ファイル操作

| キー | 動作 |
|---|---|
| `c` | 選択ファイルを反対パネルへコピー |
| `m` | 選択ファイルを反対パネルへ移動 |
| `k` | 新規ディレクトリ作成 |
| `d` | 選択ファイルを削除 |
| `n` | 新規ファイル作成 |
| `r` | リネーム |
| `Ctrl+R` | 一括リネームダイアログ |
| `Ctrl+C` | カーソル位置のファイルの絶対パスをクリップボードにコピー（選択ありの場合は選択ファイル群を改行区切りで） |
| `a` | 属性変更 |
| `f` | ファイル検索ダイアログ |
| `p` | 選択ファイルをアーカイブ化 |
| `u` | カーソル位置のアーカイブを展開 |

### ビュアー / パネル

| キー | 動作 |
|---|---|
| `v` | ビュアーで開く |
| `s` | このパネルのソート・フィルタ設定ダイアログ |
| `Ctrl+O` | シングル／デュアルパネル切り替え |
| `Ctrl+L` | ログペインの表示切替 |

### ブックマーク / 履歴

| キー | 動作 |
|---|---|
| `b` | 現在のパスのブックマーク登録／解除（トグル） |
| `Ctrl+B` | ブックマーク一覧ウィンドウ |
| `h` | ディレクトリ履歴ウィンドウ |

### アプリケーション

| キー | 動作 |
|---|---|
| `Ctrl+,` | 設定ダイアログ |
| `Ctrl+Q` | 終了 |

### 任意のビュアーで開く / ファイル実行

- `Ctrl+Enter` (`view.choose`): カーソル行のファイルを Text / Image / Binary
  どのビュアーで開くかを小ポップアップから選択。Settings の対応表に依らず強制ルーティング。
- `Shift+Enter` (`file.execute`): カーソル行のファイル / ディレクトリを
  OS 既定アプリで開く (内部的に `QDesktopServices::openUrl`)。実行可否はログに残る。
- ファイルリスト行を **マウスでダブルクリック** したときも `file.execute` と
  同じ「OS 既定アプリで開く」処理を行う (`..` 行は対象外)。

---

## ダイアログの共通仕様

全てのダイアログは「マウスを使わずキーボードだけで完結できる」ことを前提に
設計する。以下のルールを全ダイアログで一貫して守る。

### ボタン配置

- 左＝キャンセル系（Cancel / Close 等）、右＝実行系（OK / Copy / Overwrite /
  Go / Apply 等）。複数の実行系が並ぶ場合、より破壊的／より確定的なものを
  右端に置く。
- `QDialogButtonBox` を使う場合は Qt がプラットフォーム標準の並びに
  整えてくれるので、それに従う（macOS なら自動で Cancel → OK）。

### ショートカットキー

- OK / Cancel を含む全てのボタンに **Alt + 英字** のショートカットを割り当てる
  （`utils::applyAltShortcut()` ヘルパを利用）。
- ショートカットの表記はプラットフォーム慣習に揃える:
  - **macOS**: ラベル末尾に `(⌥X)` を追加。Apple HIG が `&` mnemonic を
    描画しないため、サフィックス表記でショートカットを視認させる。
  - **Windows / Linux**: ラベル中の該当文字の前に `&` を挿入し、Qt の
    ネイティブ描画（Alt 押下時のアンダーライン）を使う。ラベルに該当文字が
    無い（例: 日本語訳の「コピー」+ `Key_C`）場合は、Windows 和文ボタンの
    慣習に揃えて末尾に `(&X)` を追加（例: `コピー (C)`）。
- ラベル系ウィジェット（`QLabel` のバディ指定、`QRadioButton` 等）にも
  同じ規則で `Alt + 英字` を割り当てる（`utils::withAltMnemonic()` ヘルパ）。
- `Enter` はデフォルトボタン（右端の実行系）を、`Escape` は Cancel 系を呼び出す。
  この 2 つはラベルには明記しない。
- Yes/No 確認ダイアログは上記に加え、`Y` / `N` キー単押しでも応答できる
  （`utils::confirm()` ヘルパが keyPressEvent + eventFilter で吸収）。
- 入力フィールド（コンボボックス、ラインエディット等）にフォーカスを
  移すショートカットも `Alt + 英字` で割り当て、ラベルに明記する
  （例: macOS は `On overwrite (⌥O):`、Windows / Linux は `On &overwrite:`）。

### Tab ナビゲーション

- ボタンに限らず、操作対象になり得るウィジェット（コンボボックス・
  ラインエディット・テーブル等）にも Tab でフォーカスが回るようにする。
  各ウィジェットに `setFocusPolicy(Qt::StrongFocus)` を明示し、
  macOS の「キーボードナビゲーション」設定に依存しないようにする。
- Tab 順序は「入力フィールド → キャンセル系 → 実行系」で、**実行系は
  最後**に回す。Tab 連打で誤って実行ボタンにフォーカスしないよう
  配慮する。`setTabOrder()` で順序を明示する。
- `QTableView` / `QTableWidget` / `QTreeView` 系のリスト型コントロールは
  既定では Tab がセル間ナビゲーションに吸われてしまうので、検索結果
  リスト等の「Tab で抜けたい」ケースでは `setTabKeyNavigation(false)` を
  必ず設定する。セル内移動は矢印キーで行う。

### リスト / テーブル選択行の見た目

`QTableView` / `QTreeView` / `QListWidget` の派生ウィジェットは、既定では
フォーカスを失っても選択行を `palette(highlight)` (青) で強調し続けるため、
「あれ、このリストにフォーカスがあるのかな」と紛らわしい。Mac の
NSTableView 風に、**非アクティブ時はグレーに落とす** スタイルを共通ヘルパで
提供する。

- `utils/EnterClickFilter.h` の `inactiveSelectionStyleSheet()` を
  `setStyleSheet()` に渡すだけで適用される。
- 適用対象は「ダイアログを開いた瞬間に既定で何かが選択されているリスト」
  全般。具体的には:
  - SearchDialog (検索結果テーブル)
  - HistoryDialog / BookmarkListDialog / ShortcutListDialog / KeybindingTab
    のテーブル
  - TransferConfirmDialog のアイテムリスト
  - CSV ビュアーの本体テーブル
- 例外: SettingsDialog のサイドメニューは既に独自の `:active` / `:!active`
  指定がある。`QAbstractItemView::NoSelection` のテーブル (BulkRenameDialog
  プレビュー等) は適用不要。

「初期選択を出した上でフォーカスが他にあるときはグレー」にしたい場合は、
モデル投入後に `setCurrentIndex(model->index(0, 0))` + `selectRow(0)` を
呼んでカレント行 + 選択行を作る。これがないと最初に Tab でテーブルに移った
瞬間 1 度では選択が見えず、矢印で動かして初めてハイライトが出るので、
ユーザーから見ると「動かさないと現在位置が分からない」状態になる
(CSV ビュアー初期実装時に踏んだ罠)。

### ツールバーのスタイル

メインツールバー / 各ビュアーのツールバーは見た目を揃えるため、共通の
スタイルシートを使う。

- `utils/EnterClickFilter.h` の `toolbarStyleSheet()` を `QToolBar` に
  `setStyleSheet()` する。フォーカス枠 / `:checked` 状態 / ホバーが
  プラットフォーム間で一貫して描かれる。
- 適用対象: MainWindow / TextView / ImageView / BinaryView / MarkdownView /
  PdfView / CsvView のツールバー。新規ビュアーを追加するときも同じ
  ヘルパを使うこと。
- ツールバーのボタンに対しては `EnterClickFilter::installOnButtonsIn()` を
  使って Enter キーをクリック扱いにする (ツールバー上でも Tab → Enter で
  操作が完結する)。

---

## ファイル操作

### コピー・移動

- **転送確認ダイアログ**: コピー元、ファイル一覧、コピー先、上書きモード、
  自動リネームテンプレートを確認してから実行する。
  - **コピー先 (Destination)** は読取専用 QLineEdit + フォルダ参照ボタン。
    自由入力で誤ったパスにならないようにテキストは編集不可とし、値の変更は
    以下の 3 経路のみ:
    - ↑ / ↓ キー: 「コピー元」⇔「反対側ペイン (起動時のデフォルト)」を
      トグル切替。同フォルダ内コピーや単純な反対ペインへのコピーを 1 ストロークで
      行えるようにするための省入力。
    - フォルダ参照ボタン (右端): `QFileDialog::getExistingDirectory` を開いて
      任意のディレクトリを選択。
    - フォルダ参照ボタンにフォーカスがあるときは Enter / Return でも参照ダイアログを開く
      (ダイアログの default button = OK には流さない)。
  - 初期値は反対側ペインのカレントディレクトリ。
  - 開いた直後はコピー先入力欄にフォーカス + テキスト全選択。
  - フォルダ参照ボタンは Tab で到達したことが視認できるよう、
    フォーカス時にハイライト色の枠を描画する (macOS の標準 focus ring が
    弱いことの補正)。
- 上書き時の個別確認ダイアログ: 上書き／スキップ／リネームして保存／
  全てに適用、など。
- 複数ファイル・ディレクトリ対応、バックグラウンドスレッドで実行。
- **進捗ダイアログ**: 現在処理中のファイル名 / 進捗バー / 件数表示
  (`10 / 235 files (4%)` 形式) / キャンセルボタンを表示する。
  - 開始前に対象を再帰スキャンして総ファイル数を確定させ、
    `filesDone / filesTotal` をワーカーから逐次通知して反映する。
  - 「完了したら自動的に閉じる」チェックボックスをダイアログに常設。
    その場限りの上書きで、Settings には保存しない。
  - 既定値は Behavior タブの `Auto-close progress dialog` で指定
    （初期値 OFF）。OFF のときは完了後もダイアログを残し、
    キャンセルボタンが Close ボタンに切り替わる。
- 自動リネームの suffix テンプレートは Behavior タブで設定可能
  （`{n}` がカウンタプレースホルダ、デフォルト ` ({n})`）。

### 削除

- 確認ダイアログで「ゴミ箱へ移動 / 完全削除」を都度選択できる。
- 既定はどちらかを Behavior タブで設定（`defaultDeleteToTrash`、初期値は
  ゴミ箱）。ダイアログ上の選択はその場限りで、保存はしない。

### 圧縮・解凍

- ライブラリ: libarchive（3.x）をリンクして利用
- 対応フォーマット: zip / tar / tar.gz / tar.bz2 / tar.xz
- 操作: `p` でアーカイブ作成、`u` でアーカイブ展開
- 作成ダイアログ: フォーマットを選択、出力先は既定で反対パネル、ファイル名は
  選択内容から自動生成（拡張子はフォーマットに合わせて更新）
- 展開ダイアログ: 対象アーカイブを表示、出力先は既定で反対パネル
- バックグラウンドワーカー（`ArchiveCreateWorker` / `ArchiveExtractWorker`）
  で処理し、進捗ダイアログでキャンセル可能
- ディレクトリは再帰的にアーカイブ化、シンボリックリンクは追跡しない

### アーカイブ内ブラウジング

圧縮ファイル (`.zip` / `.tar` / `.tar.gz` / `.tar.bz2` / `.tar.xz`) を Enter で
開いたとき、バイナリビュアーで開くのではなく、**アーカイブ内のファイルツリーを
ファイルリストにそのまま表示**する。Total Commander / Double Commander の
"archive virtual filesystem" と同じ概念。

#### 動作仕様

- **読み取り専用**: アーカイブそのものは絶対に書き換えない
  - アーカイブ内への新規追加・上書き・削除は不可
  - これらの操作を試みた場合は警告ダイアログで明示
  - 一度でも書換えを認めると `.tar.gz` のような single-stream 形式で
    全データ再構築になりリスクが高いため、潔く完全 read-only に倒す
- **アクティブ化**: 通常ファイルと同じく Enter キー (or ダブルクリック)
  - `u` キー (アーカイブ展開ダイアログ) は別機能として残す。Enter は中を
    "覗く" 操作、`u` は "外に出す" 操作という棲み分け
- **ナビゲーション**:
  - Enter (アーカイブ内のサブディレクトリ) → そのサブディレクトリへ移動
  - Enter (アーカイブ内の通常ファイル) → 一時ファイルに展開して既定ビュアーで開く
    (テキスト / 画像 / バイナリ判定は通常時と同じロジック)
  - Backspace / 親ディレクトリへ → アーカイブ内ならその親、ルートなら
    アーカイブを抜けて元のファイルシステムに戻る
- **アドレスバー表示**: `~/path/to/archive.zip!/inner/dir/`
  - `!` は通常のパスに使われない区切り文字として利用
  - クリックでアドレスバー編集可、編集後 Enter で該当パスへジャンプ
- **ファイルリストの表示**:
  - 通常列 (名前 / 種別 / サイズ / 更新日時 / 権限) はそのまま表示
  - 追加情報として圧縮前サイズ・圧縮率を Tooltip 等で出す (任意)
  - `..` (親ディレクトリ) 行はアーカイブのルートのみ通常 FS への "出口" として表示
- **ファイル操作の扱い**:
  - **アーカイブ内 → 通常 FS** (展開):
    - コピー (`c`) で反対パネルに該当エントリを展開 (1 件 / 複数選択 / サブディレクトリ全部 OK)
    - これは内部的には「対象エントリだけを抜き出して書き出す」処理
  - **アーカイブ内同士 / 通常 FS → アーカイブ内**:
    - コピー (`c`)・移動 (`m`)・削除 (`d`)・新規ディレクトリ (`k`)・新規ファイル (`n`) 全て不可
    - 試行時は「Read-only archive」と表示
- **メタ情報**: Unix permissions / DOS attributes / mtime / 圧縮前後サイズを
  読み取り、可能な範囲でファイル一覧の列に反映
- **検索 / フィルタ**: アーカイブ内でも `f` (検索) と即時フィルタは動作させる
  (アーカイブのエントリ一覧を対象に in-memory 検索)

#### 制約 / 非対応

- **パスワード付きアーカイブ**は対応済み (ZIP / 7z / RAR、libarchive がサポート
  する形式のみ)。Enter でアーカイブを開いた際、暗号化エントリを検出すると
  パスワード入力ダイアログ (`QLineEdit::Password` モード) が出る。
  - 入力直後に 1 件目の暗号化エントリで試し読みして検証する
    (`ArchiveContext::verifyPassword`)
  - 間違いなら再プロンプト、3 回失敗で中止 → 通常 FS に戻る
  - 正解なら `ArchiveContext::password` に保存し、以後のリスト閲覧 / Enter 展開 /
    コピー (`c`) で `archive_read_add_passphrase` 経由で利用
  - パスワードはプロセスメモリ内のみ、永続化しない (アプリ終了で消える)
- **7z / rar / iso** は libarchive のビルド時オプションに依存。Homebrew /
  apt の libarchive がサポートしていれば動く程度のスタンス
- **巨大アーカイブ** (数 GB 級) はエントリ列挙だけでも時間がかかる場合がある →
  バックグラウンドワーカー (`ArchiveBrowseWorker` 仮称) で読み込み、進捗を出す
- **ネスト**: アーカイブ内のアーカイブを Enter で更に潜るのは将来検討 (v1.0 では
  最初の 1 段階のみ)

#### 実装メモ

- libarchive の API で `archive_read_open_filename` → `archive_read_next_header`
  反復でエントリのフルパス・サイズ・mtime・mode を取得
- 取得したフラットなエントリ列をディレクトリツリーに変換 (`/` で split し
  `QMap<QString, ArchiveEntry>` のような構造で保持)
- `FileListModel` に「仮想モード」フラグを追加するか、`VirtualArchiveModel` を
  派生クラスとして用意
- 各エントリの open は `archive_read_extract` で一時ファイル
  (`QStandardPaths::TempLocation`) に書き出し、`ViewerDispatcher` に渡す
- 一時ファイルはダイアログを閉じたタイミングで削除

### リネーム

- 単一ファイルのリネーム（ダイアログ）はカーソル行に対して実行 (`r` キー)。
  - 入力欄を開いた直後は **ベース名（拡張子を除いた部分）を選択状態**にし、
    キャレットは選択末尾＝拡張子の手前に置く。先頭の `.` (ドットファイル保護)
    は選択範囲に含めない。
    例: `foo.txt` → `foo` を選択（キャレットは `.txt` の直前） /
        `foo.tar.gz` → `foo` を選択（キャレットは `.tar.gz` の直前） /
        `.gitignore` → 全選択 / `Makefile` → 全選択。
    そのままタイプすればベース名を上書き、`End` を押せば末尾に追記、
    `→` キーでキャレット位置のまま選択解除、というフローを 1 ストロークで
    切り替えられる。コピー衝突時の OverwriteDialog のリネーム入力欄と
    アーカイブ作成ダイアログ (`CreateArchiveDialog`) のファイル名入力欄も
    同じ挙動。
- **一括リネーム** (`Ctrl+R` または File メニュー → Bulk Rename...):
  - 選択された複数ファイル (なければカーソル行 1 件) を対象に開く専用ダイアログ。
  - 入力:
    - **テンプレート**: `{name}` `{ext}` `{n}` `{n2}` `{n3}` `{n4}` `{n5}` で
      新名を生成。`{n}` のゼロ埋め幅は「Default Pad」で指定。
    - **検索 / 置換**: テンプレート展開後の名前に対して文字列または正規表現で
      置換。Regex ON のときは `\1` でキャプチャグループ参照可能。
    - **連番**: 開始値・間隔・既定の桁数。
    - **大文字 / 小文字**: そのまま / 小文字 / 大文字。
  - **プレビュー** が下半分にリアルタイム表示。エラー (空名・不正文字・バッチ
    内の重複・既存ファイルとの衝突) は赤字＋tooltip で示し、エラーがある間は
    Rename ボタン無効。
  - 結果は逐次 rename → ログに 1 件ずつ記録。失敗があればまとめて警告ダイアログ。

### プロパティダイアログ

カーソル位置のファイル / ディレクトリの詳細情報を表示するダイアログ。
`Alt+Enter` (`file.properties`) で開く。File メニューおよびキーバインド一覧 /
Settings → Keybindings からも到達可能。

- 表示項目（フォーム形式、ラベル列は左寄せ）:
  - 名前 / フルパス
  - 種別 (ファイル / ディレクトリ / シンボリックリンク)
  - サイズ
  - 各種日時 (更新 / 作成 / 最終アクセス)
  - パーミッション (`rwxr-xr-x` 形式) — **macOS / Linux のみ**
  - 所有者 / グループ — **macOS / Linux のみ**
  - リンク先 — **対象がシンボリックリンクのときのみ**（Windows では常に非表示）
- プラットフォーム別の行非表示は `QFormLayout::setRowVisible()` で行い、
  該当しない項目はラベルごとダイアログから消えて縦サイズが詰まる
  （Windows での POSIX 概念欠落、および非シンボリックリンクでの "—" 表示の
  無駄を避ける目的）。Settings → Behavior → List Display の列トグルと
  同じ「Windows-aware visibility」方針。
- **ディレクトリの場合は再帰的にサイズを計算する**:
  - 別スレッド (`PropertiesWorker`) で配下を走査し、総バイト数 /
    総ファイル数 / 総ディレクトリ数を 256 件単位のバッチでダイアログに
    流し込み、サイズラベルを逐次更新する。
  - シンボリックリンクは追跡しない（無限ループ防止）。
  - キャンセル可能。ダイアログを閉じる / `closeEvent` 受信時に
    `requestCancel()` → `wait(2000)` でワーカーを確実に終了させてから破棄する。
  - 完了するまでサイズ表示は「計算中...」相当。
  - 巨大ツリー (100GB 級) でも UI が固まらないよう、走査中も他操作可。
  - 計算結果はそのファイル / ディレクトリ専用にキャッシュしない (毎回新規計算)。
- ファイル数の表示は Qt の plural form を使い、`%n file(s), %2 directories`
  のように日英どちらでも自然な表記にする。
- 内容は読み取り専用。属性変更が必要なら別途「属性変更」(`a` キー) を使う
  住み分け。

---

## ファイル検索

- `f` キーでファイル検索ダイアログを開く。
- 指定項目:
  - 開始ディレクトリ（アクティブパネルの現在パスが初期値、Browse で変更可）
  - 名前パターン（glob、空白区切り複数、空ならすべて）
  - サブディレクトリを含むか（既定は ON）
- `Search` で別スレッドの `SearchWorker` が `QDirIterator` で走査し、結果を
  逐次テーブルに追加する。検索中は `Stop` で停止可能。
- 結果テーブル: Name / Path / Size / Modified。ダブルクリック or `Go to` で
  アクティブパネルがそのファイルのある位置を開き、当該ファイルにカーソルを
  合わせる。
- **追加フィルタ** (検索ダイアログの「Filters」グループ):
  - **サイズ**: Min / Max を `B / KB / MB / GB` 単位で指定。0 (Any) で上限/下限なし。
  - **更新日時**: From / To を `yyyy-MM-dd HH:mm` 形式で指定。
  - **内容テキスト**: ファイル内のバイト列を検索。Case sensitive 切替。
    過大ファイル防護のため 50 MiB 超のファイルはスキップする。
  - 各フィルタは独立した有効化チェックボックスで ON/OFF 可能。AND で組み合わさる。

---

## ドラッグ＆ドロップ

ファイル / ディレクトリの転送を、ファイルリスト上のドラッグ＆ドロップでも行える。

### 外 → farman（受け取り）

- 他アプリ（Finder / Explorer / 他のファイラ等）のオブジェクトを farman の
  ファイルリスト上にドロップすると、`Copy / Move / Cancel` 選択ダイアログが出る。
  - ドロップを受け取ったパネルがアクティブパネルに切り替わる。
  - 同一ディレクトリ内へのドロップ（実質的に何もしない操作）はその場で破棄。
  - 上書きモードは `Ask`、自動リネームは Behavior タブの suffix テンプレートに従う。
- バックグラウンドワーカー (`CopyWorker` / `MoveWorker`) で実行され、
  通常のコピー・移動と同じ進捗ダイアログを使う。

### farman → 外（送り出し）

- ファイルリストでアイテムをドラッグすると、選択ファイル群（または
  カーソル行 1 件、`..` を除く）の絶対パスを `text/uri-list` MIME 形式で渡す。
- macOS の慣習に従い、同一ボリューム内へのドロップは「移動」、別ボリューム
  へのドロップは「コピー」になる（受け取り側 OS の判断）。

---

## 外部変更の自動反映

farman の外部（Finder / Explorer / シェル / 他プロセス等）からカレント
ディレクトリにファイルが追加・削除・リネームされた場合に、ファイルリスト
表示を自動更新する。

- 各パネルが `QFileSystemWatcher` で表示中ディレクトリを監視。
- 連続イベントでの再描画を抑えるため 150ms のデバウンスを挟む。
- 更新時は **カーソル位置のファイル名を保存** → モデル再構築 → 同名行に
  カーソルを戻す（同名が無くなっていた場合は近傍行に置く）。
- パネルがディレクトリを切り替えると watcher の対象も切り替わる。

---

## 外部アプリ連携

farman 単独で完結しない作業 (シェル操作・テキスト編集など) を、
ユーザーが指定した外部アプリへ素早く渡せるようにする。

### データモデル: UserCommand

任意件数の **UserCommand** エントリを `settings.json` の `userCommands`
配列に保持する。組み込みの「ターミナル」「テキストエディタ」も同じ型で
扱い、`builtin=true` / `builtinKind="terminal"|"editor"` を立てて区別する。

| フィールド | 内容 |
|---|---|
| `id` | コマンド ID。組み込みは `user.cmd.terminal` / `user.cmd.editor` 固定。任意エントリは `user.cmd.<uuid>` |
| `name` | Tools メニュー / Settings 一覧上の表示名 |
| `program` | 実行ファイル (絶対パス or PATH 上のバイナリ名) |
| `argsTemplate` | 引数テンプレート。`QStringList` で 1 引数 = 1 要素 |
| `workingDirTemplate` | 作業ディレクトリのテンプレート。空ならアクティブペインの cwd |
| `showInToolsMenu` | Tools メニューに項目を出すか |
| `builtin` | 組み込みフラグ (`true` なら削除不可・ID 固定) |
| `builtinKind` | `"terminal"` / `"editor"` / 空 |

各エントリは `CommandRegistry` に **動的登録** され、Keybindings タブで
任意のキーを割り当てられる。組み込み 2 件のデフォルトキーは
`T` (terminal) / `E` (editor)。

### プレースホルダ展開

| キー | 展開内容 |
|---|---|
| `{dir}` | アクティブペインのカレントディレクトリ |
| `{otherDir}` | 反対ペインのカレントディレクトリ |
| `{file}` | カーソル位置のファイル / ディレクトリの絶対パス |
| `{files}` | 選択ファイルの絶対パス群。選択無しなら `{file}` にフォールバック |
| `{name}` | カーソル位置の名前 (拡張子含む) |
| `{ext}` | カーソル位置の拡張子 (先頭 `.` 無し、小文字) |

引数全体がプレースホルダ単体 (`"{files}"`) のときだけ **複数引数に分解展開**
する。例: `["edit", "{files}"]` + 選択 3 件 → `["edit", "/a", "/b", "/c"]`。
引数の途中に混ざる場合 (`"out-{name}.log"`) は文字列結合で展開し、
`{files}` は最初の 1 件のパスを使う。

### プラットフォーム別の初期 seed

`Settings::applyDefaults()` で組み込み 2 件を投入する。

| OS | Terminal | Editor |
|---|---|---|
| macOS | `/usr/bin/open` `-a` `Terminal` `{dir}` | `/usr/bin/open` `-a` `TextEdit` `{file}` |
| Windows | `cmd.exe` `/K` `cd` `/D` `{dir}` | `notepad` `{file}` |
| Linux (Ubuntu 24.04 LTS 基準) | `gnome-terminal` `--working-directory={dir}` | `gnome-text-editor` `{file}` |

Linux のデフォルトは Ubuntu 24.04 LTS 標準を採用 (`gnome-text-editor` は
22.10 以降の標準テキストエディタ)。他ディストリ / DE で動かない場合は
Settings → General → External Applications でユーザーが書き換える。

### Tools メニュー

メニューバーの **View と Go の間** に「Tools」メニューを置き、
`UserCommand::showInToolsMenu = true` のエントリを並べる。
`UserCommandManager::userCommandsChanged` シグナルを購読して、
Settings 変更のたびに `MainWindow::rebuildToolsMenu()` で作り直す。
組み込み 2 件以外がまだ無い段階では「(no external commands configured)」
を disabled で表示する (空メニューが消える Qt 仕様の回避)。

### Settings (External Apps タブ)

外部アプリ関連の設定は **専用タブ** に分離する (Settings ダイアログ内で
General / Behavior / Appearance / Viewers の後ろ、Keybindings の前)。
Keybindings タブと位置的に隣接させているのは、`T` / `E` 等のキーが指す先が
このタブのエントリだからで、両者を行き来しやすくする狙い。

- **Terminal** (組み込み専用 UI)
  - **Preset** コンボ (一番上) — 検出されたインストール済アプリを並べる。
    選ぶと Program / Arguments / Working Dir が自動入力される。フィールドを
    手動で編集すると `(Custom)` に戻る。
  - Program (実行ファイル名 or 絶対パス。Browse ボタンで参照ダイアログ)
  - Arguments (1 行表記。内部で `QProcess::splitCommand()` により `QStringList` 化)
  - Working Dir (空欄 = アクティブペインの cwd)
  - Test launch ボタン (現在のアクティブペインのコンテキストで実起動)
- **Text Editor** (組み込み専用 UI)
  - 同上
- **ユーザー定義コマンド** *（フェーズ 2 で UI 追加予定）*
  - 内部モデルは既に `UserCommand` で統一済み。`builtin=false` のエントリを
    External Apps タブから追加 / 編集 / 削除 / 並び替えする UI を後続で被せる。
  - 現状は手で `settings.json` に書けば動作する (Tools メニューに出る)。

### インストール済アプリのプリセット検出

External Apps タブを開いた時点で、各 OS で「インストール済」と判定された
アプリの一覧をコンボボックスに並べる。検出ロジックは
`src/core/AppPresets.{h,cpp}` で、起動時の同期処理 (10〜20 個のパス確認のみ
なので体感ゼロ)。

**検出方法 (プラットフォーム別)**:

| OS | 検出パス / 方法 |
|---|---|
| macOS | `/Applications`, `~/Applications`, `/System/Applications/Utilities`, `/System/Applications` の下に `.app` バンドルが存在するか + `code` / `subl` / `cursor` 等の CLI ラッパは `QStandardPaths::findExecutable()` で PATH 確認 |
| Linux | `QStandardPaths::findExecutable()` で `gnome-terminal` `konsole` `gedit` `code` 等を PATH 上で検索 |
| Windows | 同上 (`wt` `code` `subl` 等)。`cmd.exe` `notepad` のようなビルトインは無条件で常に出す |

**検出される代表的なプリセット**:

| カテゴリ | macOS | Linux | Windows |
|---|---|---|---|
| Terminal | Terminal.app, iTerm2, Warp, Ghostty, WezTerm, Alacritty, kitty (すべて `open -a` 経由) | gnome-terminal, konsole, xfce4-terminal, tilix, alacritty, kitty, wezterm, xterm | Windows Terminal (`wt`), Command Prompt, PowerShell (5/7+) |
| Editor | TextEdit, Visual Studio Code, Cursor, Sublime Text, Zed, BBEdit | gnome-text-editor, gedit, kate, mousepad, code, cursor, subl | Notepad, VSCode (`code`), Cursor, Sublime Text, Notepad++ |

`code` / `subl` / `cursor` のような CLI ラッパが PATH にあるアプリは、より
軽量な「(<App> CLI)」プリセットを別エントリとして上位に出す
(例: 「Visual Studio Code (code CLI)」と「Visual Studio Code」が両方ある場合、
CLI 版を優先表示)。

**workspace 系エディタ (VSCode / Cursor / Sublime Text)**:

`/usr/bin/open -a "<App>" <path>` 経由でフォルダパスをエディタに渡すと、
既起動の場合にフォルダ引数が無視される (or workspace として開かれない)
ことがある。そこでこれら workspace 系エディタは、PATH に CLI ラッパが
無くても **バンドル内に同梱されている CLI スクリプトを直接呼ぶ** ように
プリセットを構築する:

| アプリ | バンドル内パス |
|---|---|
| Visual Studio Code | `/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code` |
| Cursor | `/Applications/Cursor.app/Contents/Resources/app/bin/cursor` |
| Sublime Text | `/Applications/Sublime Text.app/Contents/SharedSupport/bin/subl` |

これにより、ユーザーが「Shell Command: Install '<X>' command in PATH」を
実行していなくても、フォルダ上で `E` を押せばそのフォルダが workspace
として開く。

**Preset 選択の挙動**:

- コンボの先頭は常に `(Custom)`。続いて検出されたプリセットを並べる。
- ロード時、現在の Program / Arguments / Working Dir が検出済みプリセットの
  いずれかと **完全一致** すればそのプリセットを選択。一致しなければ
  `(Custom)` を選ぶ (`AppPresets::matchPresetId()`)。
- ユーザーがプリセットを選ぶ → 対応する 3 フィールドを `setText()` で上書き。
- ユーザーが任意のフィールドを手で書き換える (`textEdited` シグナル)
  → コンボを `(Custom)` に戻す。`setText()` は `textEdited` を発火しないので、
  プリセット選択時の自動入力と衝突しない。
- 検出されないアプリ (例: 非標準パスにインストールしたもの) を使いたい場合は
  Browse ボタンで実行ファイルを選び、Arguments を手で書く。プリセットは
  あくまで「ワンクリック設定」のショートカットに過ぎず、自由設定の経路は常に
  併存する。

### 実装メモ

- 起動は `QProcess::startDetached(program, args, cwd)` (farman を閉じても
  外部アプリは生存)。引数は `QStringList` のリスト形式で渡すので、
  シェルを介さず **インジェクション無効**。
- どうしてもシェル機能が必要な場合 (`;` `&&` 等) はユーザーが明示的に
  `sh -c "..."` を組み立てて登録する形で吸収する (farman 側で勝手にシェルを
  挟まない)。
- 起動成功時はログに `Launched: <name> [<program> <args>] (pid=<n>)` を、
  失敗時はログ + 呼び出し元へのエラーメッセージを返す (Test launch なら
  `QMessageBox::warning`、Tools メニュー / キーバインドからの起動も同様)。
- プレースホルダ展開は `src/core/PlaceholderExpander.{h,cpp}`、
  実行は `src/core/UserCommandRunner.{h,cpp}`、
  Settings ⇔ CommandRegistry の同期は `src/core/UserCommandManager.{h,cpp}`
  (シングルトン) が担当する。`MainWindow` から `setContextProvider()` で
  PaneContext 取得用ラムダを注入する。

### 拡張 (フェーズ 2)

実装済:
- 追加 / 編集 / 削除 / **並び替え** (各行の ↑/↓ ボタン。Tools メニューの
  並び順 + Settings タブの表示順がこの順序に従う) UI
- **インポート / エクスポート** (JSON ファイル経由)。組み込み terminal /
  editor は対象外。
- アイコン (任意) は省略。Tools メニュー表示 / 将来のツールバー表示の
  個別トグルもユーザニーズが薄いため見送り。

見送り:
- 同梱プリセット (`:/presets/commands/*.json` の combo) は試作したが、
  ユーザーレビューで「コンボは不要」と判断され撤回した
  (2026-05-27)。代わりに Import で外部 JSON を読み込む経路が使える。

---

## ステータスバー

ウィンドウ最下部に常時表示。表示元はアクティブパネルにより自動切替する。

### ファイルマネージャパネル表示中
- 左: アクティブパネルのフォーカス中ファイルの絶対パス (なければ空)。tooltip でも全パス表示
- 中央 (Compare / Sync Browse / ディスク使用量 の順):
  - Compare: ディレクトリ比較モードのインジケータ (OFF 時は空)
  - Sync Browse: 同期ブラウズ ON のときだけ `Sync Browse: ON` を表示
  - **ディスク使用量**: アクティブペインのカレントが属するボリュームの状態
    - 通常 FS: `N GB free / M GB (P% used)` (例: `245.6 GB free / 500 GB (51% used)`)
      - tooltip にボリューム名 / マウントポイント / ファイルシステム種別
    - クラウド同期フォルダ: `<cloud sync folder>` 表示で容量抑止
      (ホスト FS の容量が返り誤解を招くため)。検出対象パス:
      - `~/Library/CloudStorage/` (macOS File Provider; Google Drive /
        OneDrive / Dropbox / Box 等が統合されたパス)
      - `~/Library/Mobile Documents/com~apple~CloudDocs/` (iCloud Drive)
      - `~/Dropbox`, `~/Dropbox (...)`, `~/Dropbox - ...`
      - `~/OneDrive`, `~/OneDrive - ...`
      - `~/Google Drive`, `~/Google Drive (...)`
      - Windows: `%OneDrive%` / `%OneDriveConsumer%` / `%OneDriveCommercial%`
    - アーカイブ内パス (`archive.zip!/inner`): `!` 手前 (アーカイブの実 FS
      パス) のボリューム使用量を表示
    - 5 秒の polling タイマーで自動更新 (コピー / 移動 / 削除後の空き容量変動に追従)
    - ビュアー表示中はラベル非表示
- 右: アクティブパネル要約
  - 選択あり: `N / M items selected (合計バイト)` (例: `3 / 128 items selected (3.6 MB)`)
  - 選択なし: `M items` (`..` は数えない)
- 更新タイミング: カーソル移動 / 選択トグル / アクティブパネル切替 / ディレクトリ遷移 / モデル更新

### ビュアーパネル表示中
- 左: 表示中ファイルの絶対パス
- 右: 各ビュアーが提供する `statusInfo()` の文字列
  - **テキストビュアー**: `<エンコード> · <ファイルサイズ> · N lines`
    (例: `UTF-8 · 4.5 KB · 123 lines`)
  - **画像ビュアー**: `<フォーマット> · WxH [· N frames] · <ファイルサイズ>`
    (例: `PNG · 800x600 · 24 KB`)
  - **バイナリビュアー**: ファイルサイズ。8 MB の上限を超えた場合は `... · truncated to first ...`
- ビュアーを閉じるとファイルマネージャパネル側の表示に戻る

### バイトサイズの表記
- 単位は英語 (`B`, `KB`, `MB`, `GB`) で固定 (`QLocale(QLocale::English).formattedDataSize`)。
  システムロケールに依存しない。

---

## ログ

操作メッセージ (コピー完了 / 削除完了 / ナビゲーション / エラー等) を時系列で
表示するパネルを、ファイルマネージャーパネルとステータスバーの間に置く。

### 表示
- パネルのレイアウト位置: ファイルマネージャー（左右パネル）の下、ステータスバーの上。
- 等幅フォント、折り返しなし、読み取り専用。最新行が常に見えるように追記時に末尾へ
  自動スクロール。
- バッファは UI 側でリングバッファとして保持（古い行は順次破棄）。
- 表示の ON/OFF を切り替え可能。デフォルトは ON。
  - View メニュー「Toggle Log Pane」、ショートカットキー `Ctrl+L`、Settings からトグル。
- パネルの高さ（px）を Settings で指定可能。

### ファイル出力
- 表示用パネルとは別に、同じ内容をファイルへ追記出力するか選べる。デフォルトは ON。
- 出力先は **ディレクトリ** を指定する。**1 日 1 ファイル**で自動ローテーションし、
  実ファイル名は `farman-YYYY-MM-DD.log` に固定。
  - 例: 当日のファイルは `<dir>/farman-2026-04-27.log`
  - 日付が変わったタイミングで次のログ行を書く際にファイルを切り替える。
- 既定のディレクトリは `<AppConfigLocation>`（macOS では
  `~/Library/Preferences/Farman/farman` 相当）。Settings から任意のディレクトリへ変更可能。
- **保持日数 (Retention)** を Settings で指定。
  - 1 以上の値: 起動時 / ローテーション時に、その日数より古い日付ファイル
    (`farman-YYYY-MM-DD.log` パターンに一致するもののみ) を削除。
  - 「Keep forever」ON: 削除しない（永久保持）。デフォルトは 7 日。

### メッセージ書式
- 1 操作 1 行が原則。冗長な多段ログは避ける。
- 共通フォーマット: `[YYYY-MM-DD HH:mm:ss] [LEVEL] message`
  - LEVEL は `INFO` / `WARN` / `ERROR` の 3 段階。
- 主な操作の例
  - 起動: `farman started`
  - ナビゲーション: `Left/Right pane: <path>`
  - コピー / 移動: `Copy done: N item(s) → <dest>` / `Move failed: ...`
  - 削除: `Trash done: N item(s)` / `Delete done: N item(s)`
  - 作成系: `Mkdir: <path>` / `Created file: <path>` / `Rename: <old> → <new>`
  - アーカイブ: `Archive created: <path>` / `Archive extracted: <path>`

### Settings
- Behavior タブの「Log」グループに以下を配置:
  - 1 行目 (横並び): `Show log pane` (デフォルト ON) / `Height:` SpinBox /
    `Retention:` SpinBox / `Keep forever` (チェックボックス)
  - 2 行目: `Write log to file` (デフォルト ON) / `Directory:` 出力先ディレクトリ入力 + Browse ボタン

---

## ビュアー

- `v` またはファイルによっては `Enter` で起動
- 拡張子・MIME タイプから自動判別
  - Viewers タブで Text / Image 各ビュアーの「対象拡張子」「MIME パターン」を編集可能。
    Binary Viewer はどちらにもマッチしないファイルのフォールバック先 (常に最後)。
  - 評価順は **Image → Text → Binary フォールバック** で固定。両側にマッチした場合は Image が勝つ。
  - 拡張子はワイルドカード (`*`, `?`) と除外プレフィックス `!` をサポート
    (例: `c* !class` で c から始まる拡張子のうち `class` だけ除外)。
  - MIME パターンは末尾 `*` で前方一致 (例: `text/*`)、それ以外は完全一致または `inherits` 判定。
  - プラグインのビュアー（将来実装予定）にも対応可能なようにしておく。

### 非同期ロード

大きいファイルを開いてもメインスレッドが固まらないよう、各ビュアーは
ロード処理を 2 段階に分けている:

- **`prepareLoad()`** (静的 / UI 非依存): ファイル I/O と CPU 重めの処理。
  ワーカースレッドで実行可能。
  - TextView: ファイル読込 + uchardet によるエンコード判定 + デコード
  - BinaryView: ファイル読込 + hex ダンプ整形
  - ImageView: アニメ判定 + 静止画は `QImage::load`
- **`applyPreparedLoad()`** (メインスレッド限定): 完成データを UI に反映。
  - TextView / BinaryView: `setPlainText`
  - ImageView: `QPixmap::fromImage` + `setStaticPixmap`、または `QMovie` 構築

`ViewerPanel::openFile` は `QtConcurrent::run` で `prepareLoad` をワーカー
スレッドに投げ、`QFutureWatcher` + 入れ子 `QEventLoop`
(`ExcludeUserInputEvents`) で完了を待つ。メインスレッドのイベント
ループが回り続けるので、ロード中もプレースホルダ画面の不確定
プログレスバーが動く。

ロード前に `ViewerPanel` のスタックを「読み込み中…」プレースホルダ
(タイトル / ファイル名 + サイズ + パス / 不確定 QProgressBar) に切り
替え、`QApplication::setOverrideCursor(Qt::WaitCursor)` で待機カーソル
にする。完了後に各ビュアーへ切り替えてカーソルを戻す。

BinaryView では `setPlainText` 前後で `AddressHighlighter` を一時的に
ドキュメントから切り離す。さもないと `setPlainText` が発火する
`contentsChange` で全ブロック (8MB なら ~50 万行) に対して
`highlightBlock` が同期実行され、`setPlainText` が長時間ブロック
してしまう。

### 組み込みビュアー

#### テキストビュアー

- 対象ファイル
  - .txt .log .cpp .h .json .yaml 等
- 設定項目
  - フォント種別、フォントサイズ
    - 初期設定は等幅フォント
  - 行番号表示ON/OFF
    - 初期設定はON
  - エンコード
    - 初期設定はUTF-8
  - 折り返し
    - 初期設定はON
- **文字列検索**: ヘッダ（ツールバー）に常設の検索入力欄を持つ。
  - `Cmd+F` (macOS) / `Ctrl+F` (Win/Linux) で検索入力欄にフォーカス + 全選択。
    エディタで選択中のテキストがあれば検索ボックスに自動投入する。
  - `Enter` で全ヒットを黄色背景で強調表示（`QTextEdit::ExtraSelection`）し、
    次のヒットへカーソル移動。`Shift+Enter` で前のヒットへ。
  - `Esc` で入力欄からエディタ本体へフォーカスを戻す。
  - 大文字小文字を区別するチェックボックスをトグルすると、ハイライトを再走査する。
  - ヒット件数 (`5 件`) または `Not found` を入力欄の右に表示。

#### 画像ビュアー

- 対象ファイル
  - .jpg .png .gif .bmp .webp .svg .tif/.tiff .ico .psd 等
    (Settings の Image Viewer 拡張子リストで管理)
  - **PSD (Adobe Photoshop Document)**: Qt 6 同梱の imageformats プラグインに
    PSD ハンドラが無いため、自前パーサ (`src/viewer/PsdReader.{h,cpp}`) で
    「**合成済プレビュー画像**」(= Photoshop が保存時に末尾に書き出す全レイヤ
    マージ済 RGBA。Photoshop の "Maximize Compatibility" 設定が ON のときに
    自動で保存される) のみを `QImage` として取り出して表示する。`QImage::load()`
    が失敗かつ拡張子が `psd`/`psb` のときの fallback として呼ばれる。
    - **対応**: PSD v1 / 8 bpc / Grayscale or RGB / Raw or RLE (PackBits) 圧縮
    - **非対応** (ファイルマネージャのビュアーとしては過剰): PSB (v2)、
      16/32 bpc、CMYK / Lab / Indexed / Bitmap、ZIP 圧縮、レイヤー個別表示、
      "Maximize Compatibility" OFF で保存された PSD (= 合成済プレビューが
      埋め込まれていないので表示不可)
- ツールバー操作
  - **拡大縮小率** (25 / 50 / 75 / 100 / 200% プリセット + 任意入力)
  - **ウィンドウサイズに合わせる** (Fit to Window) トグル
  - **アニメ再生 / 停止** トグル
    (GIF / WebP のアニメ。1 フレームしかない静止 GIF や PNG/JPEG/BMP では
    `QMovie::frameCount() == 1` 判定で **ボタン自体が disable** される)
  - **透明部分の背景** トグル (チェッカー / 単色、Settings の色を反映)
  - **時計回り 90° 回転** ボタン (押下のたびに 0 → 90 → 180 → 270 → 0°)。
    角度ラベルがボタン右隣に表示される。**表示のみ・ファイル保存しない**。
    ファイル切替で 0° に自動リセット。Fit-to-Window や External viewer の
    「ウィンドウサイズを画像に合わせる」も回転後サイズに追従する。
  - **画像情報 (i)** ボタン: モードレスの情報ダイアログをトグル表示
- 画像情報ダイアログの内容
  - 基本: ファイルパス / フォーマット / 縦横ピクセル数 / ファイルサイズ /
    色深度 (bpp) / フレーム数 (アニメ時) / 解像度 (DPI)
  - **`QImageReader` が認識できない形式 (PSD 等) のフォールバック**:
    フォーマット名は拡張子を大文字化したもの (例: `PSD`)、サイズと色深度は
    実際にロード済の `QImage` (= `m_loadedImage`) から取得する。これがないと
    `Format: (unknown)` / `Size: -1 x -1 px` と表示されてしまう。
  - **カラープロファイル**: `QImage::colorSpace().description()` を表示
    (例: `sRGB IEC61966-2.1`, `Display P3`, `Adobe RGB (1998)`)。
    色空間情報が無いファイルでは行ごと省略。
  - **埋め込みテキスト**: `QImageReader::text()` で取れる PNG tEXt / iTXt キー、
    JPEG コメント等。
  - **Exif** (JPEG / PNG / WebP / TIFF):
    - JPEG: APP1 セグメントの `Exif\0\0` マーカー
    - PNG : `eXIf` チャンク (PNG v2.0、2017)
    - WebP: 拡張形式 (VP8X) の `EXIF` チャンク
    - TIFF: ファイル先頭の TIFF ストリーム
    主要タグ (Camera Make/Model、撮影日時、露出 / 絞り / ISO / 焦点距離 /
    フラッシュ / ホワイトバランス / 色空間、レンズ Make/Model、GPS 緯度
    経度高度日時) を表示。実装は外部ライブラリ非依存
    (`src/utils/ExifReader.{h,cpp}`)。
- ステータスバー (statusInfo)
  - `<フォーマット> · WxH [· bpp] [· N frames] · <ファイルサイズ>`
- 動作の細部
  - サイズ上限を超える画像をプレビューモードで開いた場合は、画像は
    部分デコードできないため "File too large" メッセージで拒否する
    (テキスト / バイナリは先頭 N バイト truncate 表示)。
  - 1 フレームしか持たない GIF / WebP は、サムネイル経路・通常表示経路の
    両方で静止画として扱う。
  - QMovie が invalid と判定された場合は `QImage` での同期フォールバック
    で静止画として読み直す (古い画像が残らないよう、それも失敗するなら
    ビューを明示的にクリア)。

#### バイナリビュアー
- テキスト及び画像ビュアーで対象外となっているファイル
  - アドレス・16進数配列(16個)・文字列の順に並ぶ
- 設定項目
  - 単位
    - 1Byte・2Byte・4Byte・8Byte
    - 初期設定は1Byte
  - エンディアン
    - リトル or ビッグ
    - 初期設定はリトル
  - 文字列のエンコード種別
    - 初期設定はUTF-8

#### CSV / TSV ビュアー
- 対象: `.csv` `.tsv` (Settings の `csvViewer.extensions` で変更可)。
  テキスト判定より先に判定される (.csv はテキストにマッチし得るため、
  表形式表示の CSV ビュアーを優先する)。
- 表示は `QTableView` + 自前 `QAbstractTableModel` (`CsvTableModel`)。
  **遅延ロード**: ワーカースレッドではテキスト全文のデコードと行オフセット
  index (`QVector<int> m_rowStarts`) + 最大列数の構築だけを行う。各セルの
  文字列パースは `data()` から呼ばれた瞬間に実行し、結果は LRU キャッシュ
  (最大 4096 行) に保持する。これにより 12 MB / 20 万行クラスの CSV でも
  初回表示はインデックス走査だけで終わる (= QStringList 配列を全件作る
  従来方式と比べてメモリも初回応答時間も大幅に削減)。
- RFC 4180 準拠の quoted-field パース (`"a","b,c"`、エスケープ `""` 対応)。
  行末は `\n` / `\r\n` / 単独 `\r` を許容。
- ツールバー:
  - エンコーディング選択 (Auto / UTF-8 / UTF-16LE/BE / Shift_JIS / EUC-JP /
    ISO-8859-1)。Auto は uchardet で検出。
  - 区切り文字選択 (Auto / カンマ / タブ / セミコロン)。Auto は先頭サンプル
    の投票で決定 (`.tsv` は強制的にタブ)。
  - 「1 行目をヘッダ扱い」トグル (ON で 1 行目を列ヘッダにし、データから除く)。
  - **全文検索** (Find 欄 + 前/次ボタン + 大文字小文字区別トグル + 件数表示)。
    実装は「テキスト全体への `QString::indexOf` をループ + マッチ位置を
    `m_rowStarts` を二分探索して (row, col) に逆引き」で、巨大ファイルでも
    1 パス + 1 セルあたり O(log N) で走る。ヒット位置のセルは黄色背景で
    マーキング、Enter / Shift+Enter で次/前、Cmd/Ctrl+F でフォーカス。
- 列幅自動計算 (`resizeColumnsToContents` 経由) は内部で全行を舐めて遅延
  ロードを無効化してしまうため、`QHeaderView::setResizeContentsPrecision(100)`
  でサンプル数を上限 100 行に抑えている。
- External モード時は `CsvViewerWindow` (独立 `QMainWindow`) として開く。
- 並べ替え (列ヘッダクリック) は Phase 2 以降。
- ステータスバー (statusInfo): `CSV · <行数> 行 · <列数> 列 · <エンコーディング>
  · <ファイルサイズ>`。

#### PDF ビュアー
- 対象: `.pdf` (Settings の `pdfViewer.extensions` で変更可)。
  バイナリビュアーより先に判定される。
- Qt PDF (`Qt6::Pdf` + `Qt6::PdfWidgets`、PDFium ベース) で実装。
  `QPdfView` + `QPdfDocument` の組合せで、ページのレンダリングは visible に
  なったものから遅延描画する (= 数百ページの PDF でも初回ロードは
  ヘッダ解析だけで高速)。
- ツールバー:
  - 前ページ / ページ番号スピンボックス (1〜N) / 次ページ
  - ズームアウト / ズームイン (1.25 倍ステップ、0.1〜8.0 倍にクランプ)
  - 「幅にフィット」/「ページ全体」トグル (同時 ON は無し)
  - 「連続表示」トグル (OFF = SinglePage、ON = MultiPage)
  - 全文検索 (`QPdfSearchModel`)。Find 欄 + 前/次ボタン + 件数表示。
    Enter / Shift+Enter で次/前、Cmd/Ctrl+F でフォーカス移動。
    `QPdfView::setSearchModel` で全件ハイライト + 現在ヒットへスクロール。
- テキスト選択 / ページ回転は Phase 1 ではスコープ外 (将来課題)。
  テキスト選択は `QPdfSelection`、ページ回転は
  `QPdfDocumentRenderOptions::Rotation` を介したカスタム描画が必要。
- External モード時は `PdfViewerWindow` (独立 `QMainWindow`) として開く。
- ステータスバー (statusInfo): `PDF · <ページ数> ページ · <ファイルサイズ>`。

#### Markdown ビュアー
- 対象: `.md` `.markdown` `.mdown` `.mkd` (Settings の
  `markdownViewer.extensions` で変更可)。同じ拡張子がテキストビュアー側にも
  指定されていた場合は Markdown ビュアー側を優先する (`resolveAuto`)。
- Qt 6 標準の `QTextDocument::setMarkdown(text, MarkdownDialectGitHub)` で
  CommonMark + GitHub Flavored Markdown (見出し / リスト / 表 / タスクリスト /
  取り消し線 / 自動リンク / 画像 / コードブロック (色なし monospace)) を整形
  表示する。シンタックスハイライト・数式 (KaTeX)・Mermaid は未対応 (将来課題)。
- 内部実装は `QTextBrowser`。リンクは OS 既定ブラウザで開く
  (`setOpenExternalLinks(true)`)。相対パス画像 (`![alt](path)`) は Markdown
  ファイルのあるディレクトリを `setSearchPaths` でベースに指定して解決する。
- ツールバーの「Source」トグルで生 Markdown ソースと整形表示を切り替えできる。
- 全文検索: Find 欄 + 前/次ボタン + 大文字小文字区別トグル + 件数表示。
  Enter / Shift+Enter で次/前、Cmd/Ctrl+F でフォーカス移動。整形表示・ソース表示の
  どちらでも動く (内部の `QTextBrowser` が両モードを抱えるため)。
- エンコーディングは Auto (uchardet で検出) を既定とし、Settings の指定が
  あればそれを使う。プレビューモードではサイズ上限を超えると先頭 N バイトに
  truncate して表示する (テキスト / バイナリと同じ作法)。
- ステータスバー (statusInfo): `<行数> 行 · <エンコーディング> · <ファイルサイズ>`。

### 表示モードの切替

ビュアーを **アプリ内パネルとして表示** するか、**独立した外部ウィンドウで
表示** するかをユーザーが切り替えられる。

- **Inline (デフォルト)**: `MainWindow::m_stack` 内の `ViewerPanel` に切り替え
  て表示。Enter / Esc でファイルマネージャパネルに戻る。同時に 1 ファイル
  しか開けない代わりにキーボードで完結する。
- **External**: 独立した `QMainWindow` (`TextViewerWindow` /
  `ImageViewerWindow` / `BinaryViewerWindow` / `MarkdownViewerWindow` /
  `PdfViewerWindow` / `CsvViewerWindow`) をファイル毎に新規生成して
  表示。複数ファイルを並べたり、別ディスプレイへドラッグしたりできる。
  ウィンドウは `Qt::WA_DeleteOnClose` 付きで作るので、閉じれば自動的に破棄
  される (親は MainWindow)。

### ロード中のキャンセル

巨大ファイル (数十 MB のテキスト / 100k 行越えの CSV / 高解像度画像 等) を
開いている間にユーザーを足止めしないため、Inline / External どちらでも
**Cancel ボタン + Esc / Enter** で中断できる。

- 共通基盤: `src/utils/CancellableLoadPage.{h,cpp}` の
  `CancellableLoadPage` ウィジェット。タイトル + ファイル名 / サイズ +
  indeterminate な `QProgressBar` + Cancel ボタン (default + 表示直後に
  フォーカス)。Esc は `keyPressEvent` で Cancel に転送される。
- ロードフロー: `ViewerPanel` も各 `*ViewerWindow` も以下の同じ手順:
  1. `setForFile(filePath)` で表示テキストをセット → ロードページに切替
  2. `resetToken()` で新しい `std::shared_ptr<std::atomic<bool>>` を発行
  3. `QtConcurrent::run` で `<View>::prepareLoad(..., token.get(), ...)`
  4. `waitForFutureWithEventLoop(future)` でイベントループを回しながら待つ
     (`AllEvents` で回すので Cancel クリック / Esc が届く)
  5. 結果が `ok=false` & `*token==true` → キャンセル扱い → Inline は
     `MainWindow::showFileManager()` で戻る / External は `close()`
  6. 成功なら `applyPreparedLoad` してビューに切替
- 各 `prepareLoad` は `cancelToken` を段階間 / 内側ループの両方でチェック
  する作法に統一されている (CsvView の `buildRowIndex` は 64K 文字ごと、
  BinaryView の `formatHexDump` は 256 行ごとなど)。これにより worker は
  途中で早期 return して CPU をすぐ解放する。
- **画像だけは例外**: `QImageReader::read` がデコード中に中断不能なため、
  ロード後に token を見て結果を捨てる「ベストエフォート」キャンセルになる。
  小さな画像なら気にならず、巨大画像でだけ「Cancel 押した瞬間に閉じる」
  挙動と「decode 完了後に閉じる」挙動の差が見える。

### ロード結果のロギング

Inline / External どちらの経路でも、開いたファイルの結末を Logger に流す
(プレビューは含めない: カーソル移動の度にスパムするため)。
`utils/CancellableLoadPage.h` の `logViewerLoadResult(kind, path, ok,
cancelled)` ヘルパで形式を統一する。

- 成功: `Info  Viewer loaded (CSV): /path/to/big.csv`
- キャンセル: `Info  Viewer load cancelled (Markdown): /path/to/foo.md`
- 失敗: `Warn  Viewer load failed (PDF): /path/to/broken.pdf`

External ウィンドウは `(Text, external)` のように `, external` を付ける。

切替経路:

- **Settings → Viewers タブ → Viewer Display グループ → Display mode**
  (永続設定。`settings.json` の `behavior.viewerMode` に `inline` /
  `external` で保存)。
- **View メニュー → External Viewer Window** (チェック付きトグル項目)。
  選択する都度 Settings に書き込み、即時反映される。`Settings::save()` まで
  含めて完結するので、再起動後も状態が残る。
- **コマンド `view.toggle_viewer_mode`**。Keybindings タブで任意のキーを
  割り当て可能。デフォルトキーバインドは付けていない。

実装ポイント:

- 拡張子 / MIME による「Text / Image / Binary」のルーティングは
  `ViewerPanel::resolveAuto()` (静的ヘルパ) に切り出してあるので、Inline /
  External どちらの経路でも同一判定を使う。
- External モードのとき `MainWindow::showViewerWith()` は内部 `m_viewerPanel`
  を **触らない**。ファイルマネージャパネルが見えたまま、別ウィンドウとして
  ビュアーが立ち上がる。`v` / `Enter` どちらの経路もこのルートに乗る。
- View メニューから Inline → External に切り替えた瞬間、もし `m_viewerPanel`
  が前面表示中だったら `showFileManager()` で戻す (古い表示が残らないよう)。

### 追加ビュアー *（未実装）*

将来追加したい組み込み (またはプラグイン) ビュアー。実装方針は外部
ライブラリの導入難度と実装コストを天秤にかけて決める。

- **PDF ビュアー (拡張)**
  - Phase 1 (ページ送り / ズーム / Fit / ページジャンプ / 連続表示 / 全文検索)
    は実装済 (上記 `#### PDF ビュアー` 参照)。
  - 残件: テキスト選択 (`QPdfSelection`) / ページ回転
    (`QPdfDocumentRenderOptions::Rotation` 経由のカスタム描画) / アウトライン
    (しおり) サイドバー。いずれもビュアー内描画の改修が必要で実装難度は中、
    日常的ニーズが薄いため見送り。
- **Markdown ビュアー (拡張)**
  - Phase 0 (CommonMark + GFM + 全文検索) は実装済 (上記 `#### Markdown ビュアー`
    参照)。
  - 残件: シンタックスハイライト (コードブロックの言語別カラー表示) /
    数式 (KaTeX) / Mermaid 図 / 目次サイドバー。いずれも外部 JS エンジン
    (`QWebEngineView`) もしくは独自パーサを要し、現時点では導入コストに見合う
    ニーズが無いため見送り。
- **動画 / 音声ビュアー** *(後日 or プラグインビュアー対応)*
  - 本体取り込みは依存ライブラリ + OS 毎コーデック検証が大きい (Qt
    Multimedia の native backend は macOS / Windows / Linux で挙動が
    違う) ので、**v1.0 では未対応**。プラグインシステムを正式公開した
    段階で外部プラグイン候補に回す方が現実的。
  - 対象 (動画): `.mp4` `.mov` `.m4v` `.webm` `.avi` `.mkv` 他
  - 対象 (音声): `.wav` `.mp3` `.m4a` `.flac` `.ogg` `.aac` 他
  - 候補ライブラリ: Qt Multimedia (`QMediaPlayer` + 動画用 `QVideoWidget`、
    音声単体用 `QAudioOutput`) が第一候補。コーデック対応はプラットフォーム
    のネイティブバックエンド (macOS: AVFoundation / Windows: Media
    Foundation / Linux: GStreamer) に依存する。
  - 必要機能: 再生 / 一時停止 / 停止 / シーク (スライダ + ←/→ で
    数秒単位、Shift+←/→ で大きく) / 音量 / ミュート / ループ再生切替 /
    再生速度 (0.5x / 1.0x / 1.5x / 2.0x)。
  - 動画ビュアーはアスペクト比保持で表示領域にフィットさせる。フルスクリーン
    切替も対応したい (ビュアー側で `Qt::Key_F` などにバインド)。
  - 音声単体ファイルは波形プレビューや再生位置だけのコンパクト UI で良い。
    将来的に簡易な波形描画 (peaks) を入れる余地は残す。
  - ファイルが大きいことが多いので、起動時はメタデータだけ読んで本体ストリーム
    は再生開始時にロードする (`QMediaPlayer::setSource`)。
- **CSV ビュアー (拡張)** — 完了扱い
  - Phase 1 (表形式表示 / 区切り自動判定 + 手動切替 / ヘッダ行扱いトグル /
    エンコーディング選択 / RFC 4180 quoted-field パース) と Phase 2
    (セル内全文検索 + 行オフセット index 経由の遅延ロード) は実装済
    (上記 `#### CSV / TSV ビュアー` 参照)。
  - 列ソート (列ヘッダクリックで昇順/降順切替) は **不採用** (2026-05-27)。
    日常的ニーズが薄いため実装しない。CSV ビュアー側で並べ替えが必要な
    ケースは表計算ソフトで開く想定。
- **画像ビュアー / PSD 拡張** *(後日 or プラグインビュアー対応)*
  - 現状は合成済プレビュー (Photoshop が末尾に書き出す全レイヤマージ画像)
    のみを表示している (上記 `#### 画像ビュアー` の PSD 節参照)。
  - 本体取り込みは難度が高くスコープも大きいので、**v1.0 では合成プレビュー
    のみで打ち切り**。レイヤー個別表示まで欲しくなったらプラグイン候補。
  - 残件:
    - **レイヤー単位の表示**: PSD の Layer and Mask Information セクションを
      パースし、レイヤーツリー (フォルダ / 通常レイヤー / 調整レイヤー /
      テキストレイヤー等) をサイドバーに出す。各レイヤーの表示/非表示トグル、
      blend mode + opacity を反映した合成、レイヤーマスク、グループ折りたたみ。
    - **付随**: PSB (v2、大型形式) / 16・32 bpc / CMYK・Lab / ZIP 圧縮 /
      埋め込み ICC プロファイル + DPI 抽出 (Image Resources セクション)。
    - ファイルマネージャのビュアーとしては「閲覧」スコープに留め、編集は
      Photoshop / Affinity / GIMP に任せる方針。
    - 実装難度: 高。Photoshop の各レイヤー効果 (drop shadow / bevel / 等の
      レイヤースタイル、スマートオブジェクト、テキストレンダリング、ベクター
      マスク) を完全再現するのは非現実的なので、まずは「ピクセルレイヤーの
      ベース表示 + blend mode のうち代表的なもの (normal / multiply /
      screen / overlay)」程度から段階対応するのが現実的。
- **Office 文書ビュアー** (`.docx` / `.xlsx` / `.pptx`) *(後日 or プラグイン
  ビュアー対応)*
  - 外部レンダラまたは独自パーサのどちらにせよ重量級で、**v1.0 では未対応**。
    プラグイン候補に回す。
  - 対象: Microsoft Office Open XML 形式。
  - 候補方針:
    - **A. 外部レンダラ**: LibreOffice headless / Pandoc などで PDF or HTML
      へ変換してから自前ビュアーで表示。導入は容易だが外部依存大。
    - **B. ライブラリ直読**: `libxlsxio` (xlsx) / `python-docx` 相当の
      C++ 実装 / `unoconv` など。フォーマット個別対応が必要。
    - **C. WebEngine 経由**: 既存の HTML 化ライブラリの結果を `QtWebEngine`
      で表示。レイアウト忠実度は高いが Qt WebEngine 依存が増える。
  - 実装難度: 高。最初は **読み取り専用・テキスト主体・体裁簡略** から始め、
    画像・図形・複雑なスタイルは段階的に対応する。
- これらは Viewers タブの対応拡張子ルーティングに組み込み、ビュアー固有の
  設定 (PDF のページ表示モード、CSV の区切り文字、Office のレンダリング
  バックエンド等) を Settings に持たせる。

### プラグインシステム

**v1.0 ステータス**: 内部アーキテクチャとしての基盤コードはツリーに残して
あるが、**外部公開はしない**。理由: 仕様 / ABI を実利用で検証できていない
段階で API を見せると、後方互換維持で身動きが取れなくなる。組み込み 3 ビュアー
(Text / Image / Binary) は `IViewerPlugin` 実装として登録されており、内部的
には既に「プラグイン化済み」だが、ユーザー視点では単一バイナリのアプリ。

**ユーザー向けに非公開な要素 (v1.0 で意図的に隠している)**

- `main.cpp` の `ViewerDispatcher::loadPlugins(QDir)` 呼び出し (起動時に
  外部 .dylib / .dll / .so を読まない)
- Help → Plugins... メニューと診断ダイアログ (削除済み)
- Settings → `behavior.pluginsDirectory` の編集 UI (そもそも未提供)

**残してある基盤コード**

- インターフェース: `IViewerPlugin` ([src/viewer/IViewerPlugin.h](src/viewer/IViewerPlugin.h))
  - `pluginId()` / `pluginName()` / `supportedExtensions()` /
    `supportedMimeTypes()` / `canHandle(filePath)` /
    `createViewer(filePath, parent, ctx)` / `priority()` /
    `initialize(ctx)` / `shutdown()` / `capabilities()`
- `PluginContext` 構造体 (将来 logger / settings / mainWindow 等を「引数追加
  = ABI 破壊」抜きで足せるよう用意)
- `ViewerDispatcher::registerBuiltins()` / `loadPlugins(QDir)` /
  `pluginRecords()`
- `Settings::pluginsDirectory()` / `defaultPluginsDirectory()` アクセサ
  (JSON キー `behavior.pluginsDirectory` の serialize / deserialize も含む)

**v1.0 後にやること (バックログ)**

1. サンプル外部プラグインを社内で 1〜2 件試作し、API の使いにくさ / 抜けを洗う
2. 必要なら `IViewerPlugin` を破壊的に改訂 (この段階ならまだ自由が効く)
3. Help → Plugins... メニュー復活 + プラグインディレクトリの設定 UI 追加
4. サンプル `.dylib` の配布 / リファレンス文書化

**拡張余地** (将来): WDX 風 (列追加) / WFX 風 (仮想 FS) / WCX 風 (アーカイブ
フォーマット) は別 IID の `IContentPlugin` / `IFsPlugin` / `IArchivePlugin` を
新設する形で、`IViewerPlugin` には触れずに対応可能。「追加ビュアー」を組み込み
で実装するかプラグインとして外出しにするかは、ライブラリ依存の重さで判断する
(PDF は組み込み寄り、Office は外部依存が大きいのでプラグイン寄りが妥当)。

---

## Tab フォーカス連鎖

`Tab` / `Shift+Tab` を押したときのフォーカス移動を、レイアウト
(Dual / Single / Preview) を跨いで「閉じたループ」として設計している。
ユーザーが Tab を押し続けるだけで全 UI 要素を巡回できる + どこからでも
予測可能な順序で次の要素に進める、というのがゴール。

### 各ペインのローカル連鎖 (FileListPane)

各 FileListPane は次の順序で内部 widget を巡回する:

```
★ → アドレスバー → 📁 → ファイルリスト本体 → モード切替ボタン
```

- `★` (ブックマークラベル): 連鎖の頭。
- アドレスバー: 編集モード入りで全選択。
- `📁` フォルダ参照ボタン。
- ファイルリスト本体 (List / Thumbnail の active な側)。
- モード切替ボタン: フッタ右端の View Mode 選択ポップアップ。
- `Shift+Tab` は厳密に逆順。

実装は `FileListPane::eventFilter` 内で `setFocus()` を明示的に呼ぶ。
macOS の Tab ナビゲーション設定 (システム環境設定で OFF にされている
ケース) に依存しないよう、Qt 標準の連鎖には頼らない。
`QAbstractItemView::tabKeyNavigation` も `false` に設定し、Tab が
セル間移動に吸われないようにしている。

ローカル連鎖の **末尾 (mode + `Tab`)** または **先頭 (★ + `Shift+Tab`)**
に達すると、FileListPane は `focusExitForward` / `focusExitBackward`
シグナルを発行する。

### ルータ (FileManagerPanel)

`FileManagerPanel` は `focusExitForward` / `focusExitBackward` を
レイアウトモード別に振り分ける:

| レイアウト | `focusExitForward` (mode + Tab) の行き先 | `focusExitBackward` (★ + Shift+Tab) の行き先 |
|---|---|---|
| **Dual** | 反対 FileListPane の `★` (= 反対側の先頭) | 反対 FileListPane の `mode` (= 反対側の末尾) |
| **Preview** | `PreviewPane::focusFirstControl()` (= プレビュー先頭、典型はツールバー左端) | `PreviewPane::focusLastControl()` (= プレビュー末尾、典型はメインコンテンツ) |
| **Single** | 同ペインの `★` (連鎖の頭へラップ) | 同ペインの `mode` (連鎖の末尾へラップ) |

Dual モードで反対ペインに移ったときは、同時に `setActivePane()` で
アクティブペインも切替える (= フォーカスとアクティブ状態を一致させる)。

### プレビューペインの連鎖 (PreviewPane)

プレビューペインは各ビュアーごとのローカルチェーン (ツールバー →
メイン content) を持つ。`Tab` で先頭から末尾へ、`Shift+Tab` で逆順に
辿れる。

- `focusFirstControl()` / `focusLastControl()`: 現在の page から
  `nextInFocusChain()` を walk して、PreviewPane 配下にある最初/最後の
  Tab focusable な widget を探して `setFocus()` する。各ビュアーの
  `setTabOrder()` で「ツールバー → メイン content」順に並べている前提。
- 状態ページ (Empty / Loading / Unsupported / Directory) では false を
  返し、ルータは自ペインの ★ / mode にラップする (= プレビュー側を素通り)。
- プレビュー内の Tab/Shift+Tab が **チェーンの境界** に達したことは
  `eventFilter` で `hasFocusableInDirection()` を使って判定し、
  境界なら `focusExitForward` / `focusExitBackward` シグナルを発行する。
  ルータはアクティブな FileListPane の `★` / `mode` にフォーカスを戻し、
  ループを閉じる。
- プレビュー内で `Esc` を押すと、即座にアクティブな FileListPane の
  ビュー本体にフォーカスを戻す (`PreviewPane::escapePressed` シグナル
  経由)。テキスト選択 / コピーした直後に素早く操作に戻れるための動線。

### 設計上の制約

- 「ペイン切替は Tab で行わない」が原則。アクティブペインの切替は
  マウスクリック または `←` / `→` キー (アクティブ端での切替) を使う。
  Tab はあくまでフォーカス循環であり、Dual モードで結果として反対ペインに
  辿り着くのは「ローカル連鎖の続きが対向側になっている」副作用。
- Single モードでは反対ペインが居ないので Tab で外には出ない (自ペイン
  内でラップ)。「Tab を押し続けたら永久にループする」状態を保つ。

---

## アドレスバー

各パネル上部に [★] [現在のパス入力欄] [📁] の 3 ウィジェットを横並びで持つ。
見た目は Appearance タブの Address グループで設定できる（フォント・前景色・背景色）。

### 構成と挙動

- **★ ブックマークラベル** (左端)
  - 現在のパスがブックマーク登録済みならゴールド、未登録ならグレー表示。
  - クリックまたは Enter (Tab フォーカス時) で登録／解除をトグル
    （詳細は「ブックマーク」節）。
- **パス入力欄** (中央、QLineEdit ベース)
  - 通常はフレーム無し・読取専用でラベル風に見せる。
  - **クリック** または **Tab フォーカス** で編集モードに入り (フレーム表示 +
    全選択)、任意のパスを直接入力してジャンプできる。
  - **`Enter`** でナビゲート。**`Esc`** または **フォーカスアウト** でキャンセル
    (元のパスに戻す)。
  - 入力時の補正:
    - `~` / `~/...` を `$HOME` 相当に展開。
    - `file://` URI 形式 (Finder 等からのコピペ) を取り込んでローカルパス化。
    - ファイルパスを入力した場合は親ディレクトリへ移動 (たとえば
      `.app` バンドルパスを貼り付けたケース対応)。
  - 存在しない / ディレクトリでないパスは `QApplication::beep()` を鳴らして
    無視 (移動しない)。
- **📁 フォルダボタン** (右端)
  - クリック または Enter (Tab フォーカス時) で `QFileDialog::getExistingDirectory`
    を表示し、選択された位置へ移動。
  - `Tab` で到達したことが視認できるよう、フォーカス時にハイライト枠を出す。

### パス補完 (アドレスバー)

編集モードで途中までパスを打つと、続くディレクトリ候補がドロップダウンで
表示される (Total Commander / Explorer のアドレスバー補完と同様)。

- 実装: `QFileSystemModel` + `QCompleter`。`m_addressEdit->setCompleter()`
  で取り付け、編集モードに入ると Qt が自動でポップアップを出す。
- フィルタ: `QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::Drives`
  (ディレクトリのみ + 隠しディレクトリ + Windows の Drives 表示)
- 大小文字: `Qt::CaseInsensitive` (macOS / Windows の FS 慣習に合わせる)
- **先頭 `~` の展開**: `PathCompleter` (= `QCompleter` サブクラス) で
  `splitPath()` をオーバーライドし、入力が `~` / `~/...` のときは
  `QDir::homePath()` に展開してから親の `splitPath` に渡す。これにより
  ホームディレクトリ以下の候補が出る。
- 表示中 (読取専用) ではポップアップは出ない (Qt 標準挙動)。
- ポップアップの最大表示行数は 15 行。

### URI スキーム拡張 *（未実装）*

将来的に **URI スキーム** によるリモート FS 対応を想定。

- `file://` (現状のローカルパス相当)
- `smb://` (Samba 共有)
- `ftp://` / `sftp://` / `ftps://`
- 各スキームの認証・キャッシュは別途設計。

これらの拡張は「ファイルシステム抽象化レイヤ」と組で導入する。
ローカルファイルのみ動く現状の実装はその特殊ケース扱い。

---

## ディレクトリ比較

左右パネルのディレクトリ内容を突き合わせて差分を可視化し、必要なものを
コピー／同期するための機能。

### 想定する操作

- ショートカットキーまたはメニューから **比較モード**に入る。
  - 比較対象は左右ペインそれぞれのカレントディレクトリ。
- 比較結果は両ペインのファイルリスト上に**着色**で重ねて表示する。
  - 反対側に存在しない（追加された）アイテム
  - 両側にあるが内容が違うアイテム（サイズ／更新日時／オプションでハッシュ）
  - 両側で同一のアイテム
- 比較モード中はソートやフィルタの状態に追従する。

### 比較粒度

- ファイル名のみ（軽量、最初に通る）
- サイズ＋更新日時（既定）
- 内容ハッシュ（明示的に有効化したときのみ。大きいツリーでは時間が掛かるため）

### 同期操作 *（差分が出ているときに発火）*

- 「左にしかないものを右へコピー」 / 「右にしかないものを左へコピー」
- 「両側にあるが古い側を新しい側で上書き」 / その逆
- 「両側にしかないものを削除」 — 危険操作なので個別確認を強制
- 単発のファイル単位 (Enter で当該行のみ) と、まとめて (ボタン) の両方を提供。

### 出力

- 結果サマリをログに残す（追加 N 件 / 差分 M 件 / 同一 K 件 等）。
- 任意で CSV / JSON で書き出せると便利（オプション機能）。

### 性能上の注意

- 大量ファイル時はバックグラウンドワーカーで比較を行い、進捗を表示する。
- サブディレクトリの再帰比較は明示的なオプションとして提供（既定は浅い比較）。

---

## ツールバー

メニューバーの下に、頻出操作のボタンを並べた **ツールバー** を置く。
キーボード操作派には不要だが、初見ユーザーが視覚的にコマンドへ到達できる
ようにするのが目的。

- **配置 (固定セット)**: 6 グループにセパレータで区切る:
  1. New File / New Dir
  2. Copy / Move / Delete / Rename
  3. Search
  4. Bookmarks / History
  5. Single Pane / Sync Browse / Log
  6. Settings
- 各ボタンは `CommandRegistry::execute(id)` を呼び出すだけ
  (`addBtn` ヘルパが `addCmd` 相当を担当)。実体ロジックの重複は無し。
- ボタンに割り当てたキーバインドは **ツールチップに表記** する
  (`KeyBindingManager::keysForCommand()` の最初の 1 件をネイティブ表記で末尾に追加)。
- **表示 ON/OFF** トグル: View メニュー → "Toolbar" (チェック付き)、もしくは
  Settings → General → "Show toolbar" チェックボックスから切替可能。
  コマンド ID は `view.toggle_toolbar`。Settings の `behavior.showToolbar`
  に保存され、デフォルトは **ON** (新機能を発見してもらう狙い)。
- 表示スタイル: `Qt::ToolButtonIconOnly` 固定 (アイコンのみ)。ボタンの意味は
  tooltip (ラベル + ショートカット) で補足する。アイコン素材は
  `:/icons/toolbar/<name>.svg` (Lucide スタイル / monochrome、20px で描画)。
- 表示スタイル切替 (Icon / Text / IconBesideText の 3 値を Settings から選ぶ)
  は **不採用** (2026-05-27)。Icon Only で確定し、アイコンの意味が分からない
  場合は tooltip で確認する運用に揃える。実装着手後にユーザー判断でロール
  バック (`8031266` で revert)。
- カスタマイズ (ボタン集合・並び順をユーザーが変更可能にする) は
  **見送り**。現状のボタン数では設定 UI を増やすコストに対する見返りが薄い
  (ユーザー判断、2026-05-27)。
- macOS のネイティブ「Unified Title and Toolbar」(`setUnifiedTitleAndToolBarOnMac`)
  は採用していない (他プラットフォームとの挙動差を避けるため)。

---

## ショートカット一覧表示

現在のキーバインドの一覧を別ウィンドウで表示する機能 (`ShortcutListDialog`)。

- 起動方法: Help メニュー →「Keyboard Shortcuts」、もしくは `?` キー
  (`help.shortcuts`) でモードレスダイアログをトグル表示。`Esc` でも閉じる。
- ウィンドウは `Qt::Tool` フラグのモードレスダイアログで、開いたまま
  メインウィンドウを操作できる。
- 表示内容: `CommandRegistry` のすべての登録コマンドを **カテゴリ別**
  (Navigation / File / Pane / View / Bookmark / History / Application 等)
  に `commandsGroupedByCategory()` でグループ化し、太字背景色つきの
  カテゴリ見出し行を挟みながら 2 カラム (`Key` / `Command`) のテーブルに
  並べる。Settings → Keybindings タブと同じ `CommandLayout` を使うので
  並び順が一致する。
- 各コマンドの **説明文は tooltip** に出す (id を Key 列の tooltip に、
  description を Command 列の tooltip に表示)。
- キー表示はネイティブ表記 (macOS は `⌘C`、Windows / Linux は `Ctrl+C`)。
  複数バインドはカンマ区切り (`C, Ctrl+C`)。テンキー Enter (Key_Enter) は
  通常 Enter と冗長なので一覧からは除外する (バインド自体は有効)。
  macOS の NativeText で記号化される `Home/End/PgUp/PgDn` は、可読性を
  優先してそれぞれの英字表記に置換する。
- Settings → Keybindings タブで設定中の値をリアルタイムに反映
  (`Settings::settingsChanged` を受けて `rebuild()` する)。
- 単純な読み取り専用ウィンドウ。Settings の Keybindings タブで実際の
  変更を行う住み分け。
- ヘッダ部分に **インクリメンタル検索ボックス** (`Filter (key, command
  name, or id)`)。テキストが変わるたびに表示行を絞り込む (大文字小文字
  区別なし、`keysText` / `cmd->label()` / `cmd->id()` / `cmd->description()`
  のいずれかに含まれれば一致)。`clearButton` 付きで「×」一発で全件表示
  に戻せる。ダイアログ表示時 (`showEvent`) は検索ボックスにフォーカス
  + 既存テキストを `selectAll()` するので、`?` トグルで開いてすぐタイプ
  し直しできる。マッチが 0 件のカテゴリは見出し行も自動的に隠す。

### スコープ外

- 一覧の印刷機能 (Ctrl+P で QPrinter 経由のプリントや PDF 保存) は
  実装しない。一覧は `?` で随時表示でき、コピー&ペーストや OS の
  スクリーンショットで十分代替できると判断。

---

## ブックマーク

### 構成

1. **デフォルトブックマーク**（`isDefault = true`）
   - 初回起動時に存在するパスのみ自動注入: Home / Desktop / Documents /
     Downloads / Pictures / Music / Movies、および Windows ではドライブ
     （macOS の `/` は除外）
   - 削除・リネーム・並び替え すべて不可（ジャンプのみ可）
   - ユーザーが一度削除してしまった場合でも `defaultBookmarksInstalled`
     フラグで自動再注入を防止する
2. **動的検出ロケーション**（Detected locations、永続化しない）
   - ダイアログ表示時にスキャンし、存在するもののみ表示
   - 対象: `/Volumes/*`（ルートへのシンボリックリンクは除外）、
     `~/Library/Mobile Documents/com~apple~CloudDocs`（iCloud Drive）、
     `~/Dropbox`、`~/Google Drive`
   - ジャンプのみ可。編集・並び替え・永続化は不可
3. **ユーザー定義ブックマーク**（`isDefault = false`）
   - ユーザーが追加したもの。Rename / Delete / 並び替えが可能

### 操作

- パス表示領域の左端に★アイコンを常時表示する。
  - 登録済みなら塗りつぶし（ゴールド）、未登録ならグレー。
  - クリックでトグル：未登録時は名前入力ダイアログを出して追加、
    登録済みなら即削除（デフォルトブックマークは削除不可）。
- `b` キーで★クリックと同じトグル動作。
- `Ctrl+B` でブックマーク一覧ウィンドウを開く。
  - 単一テーブル（Name / Path）に上記3種が「固定 → Detected → 任意」の
    順で並ぶ。Detected には見出しなし、文字色のみで区別（デフォルトと
    同じ薄色）。
  - Go / ダブルクリックでアクティブパネルが選択パスへ遷移。

### 保存形式

- `settings.json` の `bookmarks` 配列に永続化。各エントリは
  `{"name": ..., "path": ..., "isDefault": true/false}`。
- Detected locations は永続化されない。

---

## ディレクトリ履歴

- パネル単位で最近訪問したディレクトリを最新順に保持する（最大10件）。
- `navigatePane()` 経由の遷移で自動記録し、同じパスへの再訪問は
  先頭に繰り上げて重複を解消する。
- `h` で履歴ウィンドウを開き、Go / ダブルクリックで
  アクティブパネルがそのパスへ遷移する（戻る／進むスタックではない）。
- 永続化の ON/OFF は Behavior タブで切り替え可能。
  - ON: 終了時に `settings.json` の `paneHistory` に保存、起動時に復元。
  - OFF: セッション限定で、終了時に空で上書きして残留を消す。

---

## 設定

### カスタマイズ可能な項目

Settings ダイアログは 5 つのトップレベルタブ (`1. General` / `2. Behavior` /
`3. Appearance` / `4. Viewers` / `5. Keybindings`) で構成される。
`Alt+1`〜`Alt+5` で直接タブ切替可能。
ビュアー固有の設定は Appearance ではなく専用の Viewers タブにまとめ、
ファイルリスト関連の見た目は Appearance に残す。

ボタンバーには通常の `OK` / `Cancel` / `Apply` に加え、`Reset All Settings...`
ボタンを置く。これは確認ダイアログ経由で **キーバインドを除く全設定** を
ヘッダ側のデフォルト値に戻す。Keybindings タブにある「Reset to Defaults」は
キーバインドだけを対象にするので、両者は独立している。

#### 1. General タブ

アプリ全体・起動時の設定とログ出力をまとめる。**ほぼ「アプリの設定」に
あたる項目**。日々の操作中に逐次切り替えるものは Behavior タブへ。

##### 起動時設定
- 初期表示ディレクトリ
  - 左パネル: デフォルト（Home）or 前回終了時のパス or 固定パス
  - 右パネル: デフォルト（Home）or 前回終了時のパス or 固定パス
- 終了時に確認するか（`Confirm on exit`）
- 言語設定 (`Auto (System)` / `English` / `日本語 (Japanese)`、デフォルト Auto)
  - 翻訳ファイルは Qt Linguist の `.ts` を `qt_add_translations` でビルド時に
    `.qm` 化してリソース埋め込み。`cmake --build build --target update_translations`
    で `tr()` 文字列を抽出して `.ts` を更新する。
  - 切り替えは **再起動で反映**（既存ウィジェットの再翻訳が完全には行えないため）。
- **二重起動を禁止する** (`singleInstance`、デフォルト ON)
  - すでに farman が起動している状態で 2 つ目を起動しようとしたら、
    新規プロセスは起動せず既存ウィンドウを前面に出す。
    実装は `QLocalServer` + `QLocalSocket` パターン:
    - 起動時に `QLocalSocket::connectToServer()` で既存サーバーへ接続を
      試み、繋がれば `activate` メッセージを書き込んで自分は `return 0`。
    - 既存サーバーは `newConnection` を受けて `showNormal()` →
      `raise()` → `activateWindow()` でウィンドウを前面化する。
    - サーバー名は `AppConfigLocation` のハッシュを混ぜた
      `farman-instance-<hash>` 形式。マルチユーザー / SSH 別ホスト等で
      socket 名が衝突しないようにする。
    - `listen()` に失敗した場合は機能を諦めて通常起動する (フェイル
      セーフ)。
  - OFF にすると複数インスタンスを並列に立ち上げられるようにする
    (左右別の作業を完全に分けたい等のニッチ用途)。
  - **スコープ外**: コマンドライン引数で渡したパスを既存ウィンドウに
    開かせる連携 (Finder の「farman で開く」等を想定したペイロード送信)
    は実装しない。farman は既にブックマーク / 履歴 / アドレスバー補完で
    パス遷移手段が揃っており、`activate` (前面化) のみで十分と判断。

##### ウィンドウ設定
- ウィンドウサイズ
  - デフォルト or 前回終了時のサイズ or ユーザー設定（カスタム値）
- ウィンドウ位置
  - デフォルト or 前回終了時の位置 or ユーザー設定（カスタム値）

##### ログ設定
- ログペインの表示 ON/OFF とペイン高さ (px)
- ファイル出力 ON/OFF / 出力先ディレクトリ / 保持日数 (`0` で永久)

#### 2. Behavior タブ

ファイル一覧の操作中に効く **挙動** をまとめる。即時反映。

##### デフォルトのフィルタ・ソート条件
- ソート設定
  - 第一ソート: ソート条件（名前・サイズ・種別・更新日時）、昇順/降順、
    ディレクトリの位置（先頭・末尾・区別しない）
  - 第二ソート: 有効時のみ、ソート条件と昇順/降順
  - ドットファイルを先頭に（`Sort dot files first`）
  - 大文字小文字を区別（`Case sensitive sorting`）
- フィルタ設定
  - 隠しファイルを表示するか

##### ナビゲーション設定
- カーソルループ（末尾で ↓ → 先頭、先頭で ↑ → 末尾）
- ディレクトリ履歴の永続化（ON のとき終了時に保存、起動時に復元）
- ドットファイルの 2 文字目マッチ（`typeAheadIncludeDotfiles`、初期値 ON）
  - ON のとき、`Shift+文字キー` での頭文字検索が `.gitignore` / `.claude` 等
    のドットファイル／ディレクトリの先頭の `.` を読み飛ばし、2 文字目
    （上記例なら `g` / `c`）でもマッチする

##### ファイル操作設定
- 自動リネームのサフィックステンプレート
  - 例: ` ({n})` で「foo.txt」衝突時に `foo (1).txt` を生成
- 削除の既定: ゴミ箱 or 完全削除（`defaultDeleteToTrash`）
- 進捗ダイアログを完了時に自動的に閉じる（`progressAutoClose`、初期値 OFF）
  - 進捗ダイアログ上のチェックボックスはその場限りの上書き。保存はしない。

##### List Display グループ

ファイル一覧の **データの見せ方** (装飾ではなく書式) をまとめる。
2 画面 (Dual) と 1 画面 (Single) でそれぞれ独立した値を持ち、表示中の
モードに応じて自動で切り替わる。Behavior タブ上は「Dual pane」「Single
pane」の 2 つのサブグループに分けて配置する。

各サブグループの内容:

- **ファイルサイズ表示形式** (Bytes / SI(KB/MB/GB) / IEC(KiB/MiB/GiB) / Auto、既定 Auto)
- **桁区切りカンマ** (`Comma` チェック、既定 ON)
  - サイズ形式が **Bytes のときのみ有効**。SI / IEC / Auto は単位繰り上がりで
    数値が常に 0〜1023 に収まり、カンマが入る余地が無い。Bytes 以外を選んでいる
    間はチェックボックスを disabled にして「効かない」ことを視覚的に伝える。
  - Bytes ON 例: `12,345,678 B` / Bytes OFF 例: `12345678 B`
- **日時フォーマット** (Qt 書式文字列、エディタブル、既定 `yyyy/MM/dd HH:mm:ss`)
- **列の表示** (`Columns`)
  - Name は常に表示 (固定 ON、UI でも disabled 表示)
  - 残り 9 列 (Type / Size / Modified / Created / Permissions / Attributes /
    Owner / Group / Link Target) を ON/OFF できる
  - **プラットフォームに合わない列は UI からも表示からも除外**:
    - macOS / Linux: Attributes は概ね Permissions と冗長なので非表示
    - Windows: Permissions / Owner / Group は Unix 概念なので、Qt 経由では
      `rw-rw-rw-` の疑似表現にしかならず実用性がないため非表示

旧設定 (`fileSizeFormat` / `dateTimeFormat` の単一キー) があれば、Dual /
Single の両方の既定値にコピーして移行する。

#### 3. Appearance タブ

ファイルリストの見た目 (フォント・色・行高) に関する設定をまとめる。
書式 (サイズ表示形式・日時フォーマット・列の表示) は Behavior タブの
List Display へ移動済み。ビュアー固有の設定は Viewers タブへ。
コンポーネント単位で 3 グループに分割され、各グループ内の主要項目は横並び (左寄せ) で配置される。

##### Address グループ
パネル上部のアドレスバー (現在のパス／将来は smb://, ftp:// 等の URI も
表示する想定) の見た目を設定する。
- フォント
- 文字色 (Foreground)
- 背景色 (Background)

##### Cursor グループ
- 形状: Underline (下線) / Row Background (行全体を塗る) — 既定 Underline
- Underline 時の太さ (px、初期 2)
- Active カーソル色 / Inactive カーソル色

##### File List グループ
- フォント
- 行の高さ (`Row Height`、px、初期 `Auto`)
  - `0` のときは Qt の既定行高 (フォントメトリクスから算出) を使う。
  - 1 以上を指定するとそのピクセル数で固定。
- ファイル種別ごとのカラーリング (4 状態グリッド)
  - カテゴリ: Normal / Hidden / Directory
  - 状態: 通常 (Normal State) / 選択時 (Selected State)
  - 各状態に Foreground / Background / Bold を指定可能
  - 「Use custom colors for inactive pane」ON で非アクティブパネル用の色も別途設定可能

#### 4. Viewers タブ

3 ビュアー (Text Viewer / Image Viewer / Binary Viewer) の設定を `QToolBox` による
**排他アコーディオン**で配置する (一度に 1 セクションだけ展開、ヘッダークリックで切替)。
各ビュアーの主要項目は横並び (左寄せ) で配置する。

##### テキストビューア
- 対象ファイル指定
  - Extensions: 空白区切りの拡張子リスト。ワイルドカード `*` / `?` と除外
    `!class` をサポート (例: 既定 `txt log md* c* h hpp py js ts ...`)
  - MIME patterns: 空白区切り。末尾 `*` で前方一致、それ以外は完全一致 + `inherits` 判定
    (既定 `text/* text/plain`)
- フォント (初期は OS の固定ピッチフォント)
- 文字コード (初期 UTF-8。UTF-16LE/BE、Shift_JIS、EUC-JP、ISO-8859-1 はプリセット。
  Qt5Compat の QTextCodec 経由で他のエンコードも指定可能)
- 行番号表示の ON/OFF (初期 ON)
- ワードラップの ON/OFF (初期 OFF)
- カラーリング (Foreground / Background)
  - Normal: 通常時の文字色・背景色 (Background に none を許す)
  - Selected: 選択時の文字色・背景色
  - Line Number: 行番号エリアの文字色・背景色
- これらは Settings に保存される既定値。テキストビュアー自体のツールバーで
  エンコード・行番号表示・ワードラップをその場で上書きできるが、上書き内容は保存されない。
- **エンコード自動判定**: `Auto` を選ぶと libuchardet で内容から判定し、ステータス
  バーに `<検出名> (auto)` で表示する。デフォルトは `Auto`。手動で UTF-8 等を
  選んだ場合は判定をスキップしてその指定で decode する。
- **文字列検索**: ツールバーに常設の検索入力欄、`Cmd/Ctrl+F` でフォーカス、
  `Enter` で全ヒットをハイライト + 次へ移動、`Shift+Enter` で前へ、`Esc` で
  エディタへ戻る（詳細はビュアー節「テキストビュアー」参照）。

##### 画像ビューア
- 対象ファイル指定
  - Extensions: 既定 `png jp*g gif bmp svg webp ico tif*` (ワイルドカード対応)
  - MIME patterns: 既定 `image/*`
- 拡大縮小率
  - プリセット: 25% / 50% / 75% / 100% / 200% / 任意 (ユーザー入力可能)
  - 「ウィンドウサイズに合わせる」ON 時は設定不可 (画像のフィット結果に追従して値が変化する)
  - 初期 100%
- ウィンドウサイズに合わせるか否か
  - ON のとき、ウィンドウサイズの横幅または縦幅に合わせて画像全体が収まるよう自動拡大縮小する
  - ウィンドウサイズ変更時は追従して再スケール (拡大縮小率も連動して更新)
  - OFF にした瞬間は直前のフィット結果のズーム % を引き継ぐ (画像サイズが急変しない)
  - 初期 OFF
- アニメーション再生 (ビュアー上は「▶ Play / ⏸ Stop」トグルボタン)
  - GIF / アニメ付き WebP が対象 (APNG はビルド依存で現状未対応 — 後述)
  - 再生中は ⏸、停止中は ▶ 表示。停止中は先頭フレームを静止表示
  - Settings 側のデフォルトは ON/OFF チェックボックス、初期 OFF
  - **APNG (アニメーション PNG)** は Qt の PNG プラグインが apng パッチ付き libpng で
    ビルドされている場合のみ動作する。Homebrew Qt6 を含む一般的なビルドでは
    1 フレーム静止 PNG として扱われる。独自デコーダ実装は将来課題。
- 透明部分の設定 (Transparency)
  - モードを Checker (市松模様) / Solid Color (単色) からラジオボタンで選択 (既定 Checker)
  - Checker モード: タイル 2 色を個別に指定 (Color 1 / Color 2)
  - Solid Color モード: 1 色 (Color)
  - ビュアー本体は Fit OFF でも背景がビューポート全体を埋めるよう描画する
- これらは Settings に保存される既定値。画像ビュアー自体のツールバーで
  拡大縮小率・「ウィンドウサイズに合わせる」・アニメーション再生・Transparency モード
  をその場で上書きできるが、上書き内容は保存されない (色は Settings 由来のまま)。

##### バイナリビューア
- 対象ファイル: テキスト・画像のいずれにもマッチしないファイル (拡張子・MIME での
  ルーティング設定は持たない、フォールバック専用)
- 単位 (1Byte・2Byte・4Byte・8Byte、初期 1Byte)
- エンディアン (Little / Big、初期 Little)
- 文字列のエンコード (初期 UTF-8。UTF-16LE/BE、Shift_JIS、EUC-JP、ISO-8859-1
  はプリセット。Qt5Compat の QTextCodec 経由で他のエンコードも指定可能)
- 等幅フォント (初期は OS の固定ピッチフォント)
- カラーリング (Foreground / Background)
  - Normal: 通常時の文字色・背景色
  - Selected: 選択時の文字色・背景色
  - Address: 各行先頭 8 桁のアドレス列の文字色・背景色 (`QSyntaxHighlighter` で着色)
- これらは Settings に保存される既定値。バイナリビュアー自体のツールバーで
  単位・エンディアン・エンコードをその場で上書きできるが、上書き内容は保存されない。

#### 5. Keybindings タブ

- 全コマンドのキーアサイン設定
  - ナビゲーション系コマンド
  - ファイル操作系コマンド
  - パネル操作系コマンド
  - アプリケーション系コマンド
  - ビュー系コマンド
- コマンドごとにキーバインドを変更可能
- キーバインドのクリア (Cmd+D / Ctrl+D)
- デフォルトにリセット (Cmd+R / Ctrl+R)

### 設定ファイル

- メインの設定は `settings.json`（JSON フォーマット、自前シリアライズ）。
  - 保存先は `QStandardPaths::AppConfigLocation`。
    - macOS: `~/Library/Preferences/Farman/farman/settings.json`
    - Windows: `%APPDATA%/Farman/farman/settings.json`
  - パネル設定、カラー、外観、フィルタ・ソート、ブックマーク、履歴、
    ウィンドウサイズ／位置などを一括管理する。
  - `pathOverrides` セクションに、パス単位で上書きしたソート・フィルタ
    設定を絶対パスキーで格納する。
- キーバインディングのみ `QSettings` を使用（`farman/farman` 名義）。
  - macOS: `~/Library/Preferences/com.farman.farman.plist`

### プリセット / インポート・エクスポート

キーバインドと配色は、ユーザー間や複数マシン間で共有しやすいように
**JSON ファイル単位でのインポート / エクスポート**、および
**プリセット (= farman 同梱の出来合いセット) の選択** をサポートする。

- **キーバインドのインポート / エクスポート**
  - Settings → Keybindings タブにボタン: `Export...` / `Import...`。
  - 形式: `{"version": 1, "bindings": [{"command": "file.copy", "keys": ["C", "Ctrl+C"]}, ...]}`
    のような JSON。コマンド ID は `CommandRegistry` の登録キー。
  - インポート時は既存設定を全置換するか追記/マージするかを選べる。
- **キーバインドのプリセット**
  - 同梱例: `Default (farman)` / `Total Commander 風` / `Vim 風 (h/j/k/l 主体)`
    / `macOS Finder 風` 等。リソースに JSON で同梱しておき、リスト選択で
    一括適用。
  - 「リセット」は `Default (farman)` プリセットの適用と同義。
- **配色 (カラー設定) のインポート / エクスポート**
  - Settings → Appearance タブにボタン: `Export Theme...` / `Import Theme...`。
  - 対象: アドレスバー / カーソル / ファイル種別 4 状態カラー / 行高 /
    フォント (フォントは OS 依存なのでファミリ名のみ持つ)。
  - 形式: 同様の JSON。`{"version": 1, "address": {...}, "cursor": {...},
    "categories": {...}}` 等。
- **配色のプリセット (テーマ)**
  - 同梱例: `Light (Default)` / `Dark` / `Solarized Light` / `Solarized Dark`
    / `Monokai` 等。リスト選択 + プレビュー → 適用。
  - キーバインドと違って即時切替可能 (再起動不要)。
- **ユーザー定義コマンドのインポート / エクスポート**
  - Settings → General タブの「外部アプリ」グループにボタン:
    `Export Commands...` / `Import Commands...`。
  - 形式: `{"version": 1, "commands": [{"name": ..., "command": ...,
    "args": ..., "workdir": ..., "menu": ..., "icon": ...}, ...]}`。
  - プラットフォームを跨ぐと実行ファイルパスが変わるので、
    プリセット同梱時は **テンプレート扱い** (ラベル + プレースホルダ
    規約 + 「適用前にコマンドパスを書き換えてね」のヒント) とする。
- インポート時のバリデーション
  - 未知のフィールドは警告ログを出して無視。
  - `version` で前方互換 (将来 v2 が出ても v1 ファイルは読める)。
- 「変更を保存して終了」する以外に「**設定全体のエクスポート / インポート**」
  (ブックマーク / カスタムコマンド / カラースキーム / ビュアー設定 / パス等を
  まとめた `settings.json` 全部の dump) を Settings → General → 「バックアップ /
  復元」グループから利用可能。`Settings::exportTo(path)` は save() で
  in-memory を確定してから `settings.json` を path にコピー。
  `Settings::importFrom(path)` は JSON parse + `version` 必須チェック →
  `settings.json` を上書き → `load()` → `settingsChanged()` 発火。
  ダイアログの未保存編集はインポート確認時の警告で予告して破棄する。

---

## インストーラ *（部分実装）*

ユーザーが OS 標準のインストール手順だけで farman を導入できるよう、
プラットフォームごとの配布形式を整備する。GitHub Releases に貼った
アーティファクトをダブルクリック / ドラッグ で導入完了するレベルを
目標とする。

### プラットフォーム別の形式

- **macOS** — `.dmg` (Disk Image)
  - DMG を開くと左にアプリケーションフォルダへのシンボリックリンク、右に
    `farman.app` が並ぶ「典型的な Mac インストーラ」レイアウト
    (ドラッグ先のターゲットを左、ドラッグ元のアプリを右に配置する)。
  - 配布ターゲットは Apple Silicon (arm64)。Universal Binary 化は
    バックログ参照。
  - **コード署名** (Apple Developer ID Application) と **公証 (notarization)**
    に対応済み (v0.9.5〜)。CI で `.app` を Developer ID 署名 (Hardened Runtime
    + timestamp) → `.dmg` も Developer ID 署名 → `notarytool` で公証 →
    `stapler` で staple する。配布された `.app` / `.dmg` とも `spctl` で
    "Notarized Developer ID" として受理され、Gatekeeper 警告なしで開ける。
    (証明書 + 公証用の認証情報は CI の Secrets として保持。詳細は
    「コード署名 (CI)」節を参照)
- **Windows** — `.exe` インストーラ (NSIS or Inno Setup)
  - 必要なら `.msi` も検討するが、初回は単一 `.exe` が最も摩擦が少ない。
  - Program Files / AppData の双方への install 経路をサポート (個別
    ユーザー権限でも入れられるように)。
  - **Authenticode 署名**: 未対応。当面は配布サイトの Windows ダウンロード
    カードに「SmartScreen 警告が出たら 詳細情報 → 実行」の注記を出して回避を
    案内する運用 (2026-05-29 ユーザー判断)。未署名だと SmartScreen が警告を出し、
    Windows 11 の **Smart App Control (SAC)** が ON の環境ではインストーラ自体が
    ブロックされ、再インストール (= キャッシュリセット) や Sandbox 経由でしか
    起動できないケースもある。抜本対応は Authenticode 署名の導入。コスト/手間と
    効果を踏まえると **Azure Trusted Signing** (月額が安く CI 連携しやすい。ただし
    組織検証・設立年数等の適格要件あり) が第一候補。OV/EV 証明書は 2023 年以降
    秘密鍵のハードウェア保管が必須化しており、macOS のように `.p12` を Secret に
    入れる方式が使えない点に注意。配布数が増えてユーザーから声が出てきたら検討。
  - スタートメニュー / デスクトップショートカットの作成、関連付けの
    登録 (任意)、アンインストーラの登録。
- **Linux** — `AppImage`
  - 単一バイナリで配布できるので最初の対応として最も軽い。
  - 需要次第で `.deb` / `.rpm` / Flathub / AUR を追加検討
    (バックログ参照)。

### バージョン情報

- すべての配布物に **バージョン番号 + ビルド ID** を埋め込む
  (`farman --version` で出力可能)。
- `MACOSX_BUNDLE_BUNDLE_VERSION` / Windows のリソース VERSIONINFO /
  AppImage の `*.desktop` の `X-AppImage-Version` にそれぞれ反映。

### リリースフロー (運用)

タグの命名規約:
- **`vX.Y.Z-test`**: テストビルド用のプレリリースタグ。`release.yml` が
  CI で 3 OS のアセットを作って GitHub Releases に Draft 公開する。
  ビルド時に `FARMAN_VERSION_SUFFIX=test` がタグから抽出されて
  バイナリ FARMAN_VERSION も `X.Y.Z-test` になる (semver の
  prerelease ルールで `X.Y.Z-test < X.Y.Z` と判定されるため、テスト
  ビルドから安定版への auto-update 検知が正しく働く)。
- **`vX.Y.Z`**: 正式リリースタグ。同じ `release.yml` が走り、
  サフィックスなしの正式リリースの Draft を作る。

リリース時の手順 (この順序で進める):
1. 作業ブランチで実装 + コミット + `CMakeLists.txt` の `VERSION` を bump。
2. **`vX.Y.Z-test` を作業ブランチの先端に直接** 打って push
   (main にはまだマージしない)。
3. CI のビルド + macOS / Windows / Linux での動作確認。
4. 問題が見つかったら作業ブランチに修正コミット →
   `git tag -f vX.Y.Z-test` + `git push --force origin vX.Y.Z-test`
   で Draft Release を新ビルドで更新。
5. テスト完了後、作業ブランチを **main に fast-forward マージ** + push。
6. **main の HEAD に `vX.Y.Z` 正式タグ** を打って push →
   release.yml が正式リリースの Draft を生成。
7. Draft の中身を UI で確認してから手動 Publish。

ポリシー:
- main へのマージは「テスト完了後の正式リリース直前」に限る (= main は
  常に「直近の正式リリース or その候補」を指す状態を保つ)。
- `build.yml` は push trigger から main を外してある (PR は残す)。main
  マージで CI が走らないので、リリース系の `release.yml` だけが走る。
- **force push** は `v*-test` タグに限る運用。`vX.Y.Z` 正式タグや main
  ブランチへの force push はしない。

### 既存の状態

[release.yml](.github/workflows/release.yml) で `v*` タグ push をトリガに
3 OS 分の配布パッケージをビルドし、GitHub Releases に **draft** として
公開するところまで実装済み (本人が UI で確認してから手動 Publish する運用)。

OS 別の現状:

- **macOS**: `macdeployqt` で Qt frameworks を `.app` に埋め込み →
  Developer ID 署名 (Secrets が揃っているとき) または ad-hoc 署名 →
  `create-dmg` で典型 Mac インストーラレイアウト (左に `/Applications`
  シンボリックリンク、右に `farman.app`) の `.dmg` 生成 → `notarytool` で
  公証 + `stapler` で staple。背景画像 / ボリュームアイコンの追加は
  将来課題。
  - **アイコン**: `images/icon.icns` を `MACOSX_BUNDLE_ICON_FILE` で
    バンドルに同梱し、`Info.plist` の `CFBundleIconFile` に反映。
    Dock / Finder / Cmd+Tab すべてここから読まれる。
  - **`QApplication::setWindowIcon` は macOS では呼ばない**
    (`main.cpp` 内で `#ifndef Q_OS_MACOS` ガード)。これを通すと Qt が
    バンドルの `.icns` を上書きしてしまい、Windows 用に作った真四角な
    `.ico` が Dock に出てしまう。Windows / Linux ではタスクバー / ウィンドウ
    装飾用に必要なので残す。
- **Linux**: `linuxdeploy` で AppImage と、AppDir を `/opt/farman/` に
  詰めた `.deb` の両方を生成。
- **Windows**: `windeployqt` + vcpkg DLL コピー後、Inno Setup 6 で
  `.exe` インストーラを生成 (`windows/farman.iss`)。`{autopf}\farman`
  既定 (Program Files 配下、または lowest 権限で LocalAppData)、起動時
  ダイアログで「すべてのユーザー / 自分のみ」を選択可能。スタートメニュー
  ショートカット必須 + デスクトップショートカット任意 (Tasks)、アンインス
  トーラ自動登録、64-bit 限定。ポータブル用途の `.zip` も同時生成して併売。
  関連付けの登録は未対応 (将来課題)。
  - **MSVC ランタイム DLL の同梱**: `windeployqt` は `MSVCP140.dll` /
    `VCRUNTIME140.dll` / `VCRUNTIME140_1.dll` をコピーしないため、
    `$VCToolsRedistDir` 経由で release ディレクトリに直接コピーする
    (release.yml 内で実施)。これを忘れると VC++ Redistributable を入れて
    いないクリーン環境 (Windows Sandbox など) で「MSVCP140.dll が見つからない」
    エラーになる。
  - **アイコンを exe リソースに埋める**: `windows/farman.rc.in` テンプレートを
    `configure_file` で `farman.rc` に展開 → MSVC リソースコンパイラで
    `IDI_ICON1 ICON "images/icon.ico"` を `farman.exe` に埋め込む。
    これがないと Explorer / タスクバー / Add/Remove Programs すべてで
    Windows 既定の灰色 EXE アイコンになる (Qt の `setWindowIcon` は実行時の
    ウィンドウ装飾だけで、exe リソースとは別経路)。
  - `images/icon.ico` は 32bpp RGBA + multi-size (16/32/48/64/128/256)
    で生成し、無地の白背景に角丸シルエットを乗せたもの。タスクバーは
    最大サイズを取りに行くので、Windows-friendly に padding を最小化して
    あるのが macOS / Linux のソースアイコンとの違い。
- **コード署名 (CI)**: macOS は **Developer ID 署名 + 公証まで CI で実装・
  運用済み** (v0.9.5〜)。`release.yml` が次の順で処理する:
  1. `MACOS_CERTIFICATE_BASE64` 等の Secret が揃っていれば署名フローを実行
     (無ければ ad-hoc 署名にフォールバック)
  2. 一時キーチェーンに `.p12` を import → `.app` を Developer ID 署名
     (`--options runtime --timestamp`)
  3. `create-dmg` で `.dmg` を作成 → **`.dmg` 自体も Developer ID 署名**
     (これが無いと `spctl -t open` が "no usable signature" になる)
  4. `notarytool submit` で公証 → `stapler staple` でチケットを `.dmg` に添付
  - 必要な Secrets (mashsoft-jp/farman の Actions secrets):
    `MACOS_CERTIFICATE_BASE64` (Developer ID Application 証明書 + 秘密鍵の
    `.p12` を base64 化) / `MACOS_CERTIFICATE_PASSWORD` (`.p12` のパスワード) /
    `MACOS_NOTARIZATION_APPLE_ID` (Apple ID メール) /
    `MACOS_NOTARIZATION_TEAM_ID` (10 桁 Team ID) /
    `MACOS_NOTARIZATION_PASSWORD` (appleid.apple.com で発行したアプリ用パスワード。
    `.p12` のパスワードとは別物)。
  - **macOS arm64 ランナーのビルド並列度は `--parallel 2` に制限**している。
    無制限だと少メモリランナーで重い翻訳単位 (-O2) が同時多発してメモリ逼迫 →
    clang がストールしジョブが固まる事象が頻発したため (Linux も `--parallel 2`、
    Windows のみメモリ余裕があり無制限)。
  - Windows Authenticode 署名は未対応 (上記「プラットフォーム別の形式」参照)。
- **`farman --version`**: 実装済み。`QCommandLineParser::addVersionOption()`
  で `farman X.Y.Z` を stdout 出力 + 即終了 (GUI 起動しない)。`--help`
  も同時に効く。
- **ビルド ID 埋め込み**: 未対応 (FARMAN_VERSION マクロのみ。git short SHA
  などの追加は将来課題)。

---

## 自動アップデート

farman を起動したまま手動でリリースを追わずに済むよう、新しいバージョン
が出たら知らせて取り込む仕組みを用意している。**インストーラ** と組で
動く機能で、正規アーティファクトを GitHub Releases から取りに行く。
SHA256 チェックサム (`<asset>.sha256` という名前で同じリリースに添付)
で検証する。

### チェック方針

- **起動時に最大 1 日 1 回** だけ最新版チェックを行う。前回チェックの
  タイムスタンプ (`autoUpdate.lastCheckedAt`) を Settings に保存し、
  24 時間経過していなければスキップ。
- ネットワーク先は GitHub Releases API
  (`https://api.github.com/repos/<owner>/farman/releases/latest`) または
  自前の更新マニフェスト JSON (将来 Tap / Flathub と整合させたいなら
  自前マニフェストの方が柔軟)。
- 失敗 (オフライン / 4xx / タイムアウト) は静かに諦める。次の起動で
  再試行。エラーバナーは出さない (うざい)。
- `User-Agent` に `farman/<version> <os>/<arch>` を載せ、APIレート
  リミット対策 + 利用統計の足がかりにする。

### 通知 / 適用フロー

1. 最新版が手元のバージョンより新しいと判定された場合、起動完了後の
   タイミングでモードレスダイアログを 1 回表示:
   - タイトル: `Update available — farman X.Y.Z`
   - 本文: リリースノート (Markdown を簡易整形して表示。長文なら
     スクロール可能エリアに収める)
   - ボタン: `Update Now` / `Remind Me Later` / `Skip This Version`
     - **Update Now**: ダウンロード進捗ダイアログを出して取得 → 検証
       (署名 / SHA256) → インストーラ起動 (Mac は DMG マウント &
       app 差し替え、Windows は新インストーラ実行、Linux は新 AppImage
       との差し替え)。 farman 自身は終了。
     - **Remind Me Later**: 何もせず閉じる。次回チェック (24 時間後)
       でまた表示。
     - **Skip This Version**: そのバージョン番号を `autoUpdate.skipped`
       に記録し、より新しいリリースが出るまで通知しない。
2. **オプションで完全自動更新** (`autoUpdate.silent = true`) に設定
   できる。ON のときは確認ダイアログを出さず、バックグラウンドで
   ダウンロード → ユーザーが farman を終了したタイミングで自動置換。
   (起動中の置換はしない。Mac は同名 .app を上書きするとプロセスが
   不整合になるため。)
3. **手動チェック**: Help メニュー →「Check for Updates...」で任意の
   タイミングで起動可能。最新が手元と同じなら「You're up to date.」
   ダイアログ、新しいなら通知ダイアログ (上記 1.)。

### Settings → General タブの新設

```
☑ Check for updates automatically (daily)
☐ Download and install updates without asking
[Check for Updates Now]
Last checked: 2026-05-10 09:42
```

- 1 個目のチェックを外すと完全に黙る (= 起動時の自動チェック OFF)。
  手動チェックボタンは常に有効。
- 2 個目のチェックは「ダウンロード + インストール」を確認なしで自動
  化する (前述 silent モード)。1 個目を外している間はグレーアウト。

### ストレージ

- ダウンロードしたインストーラの一時ファイルは
  `<AppCacheLocation>/updates/farman-X.Y.Z.<ext>` に置く。
  インストール完了 (or キャンセル) 時に削除。
- 正規アーティファクトの **SHA256 + 署名** をマニフェスト側に持たせて
  検証する。検証失敗したらエラー表示してアップデート中止。

### 設定 (`settings.json`) の追加キー

```json
"autoUpdate": {
  "checkOnStartup": true,         // 起動時の日次チェック
  "silent":         false,        // 確認なしで適用 (silent モード)
  "lastCheckedAt":  "2026-05-10T00:42:13Z",
  "skipped":        ["1.2.3"],    // Skip This Version でスキップ済み
  "channel":        "stable"      // 将来の beta チャンネル用
}
```

### 実装状況

実装済み (2026-05-20)。

- `src/core/UpdateChecker` — GitHub Releases API 経由のバージョン
  チェック。`User-Agent: farman/<version> <os>/<arch>` を付与。
  4xx / オフライン / タイムアウトは静かに諦める (手動チェック時のみ
  簡潔なステータスを表示)。`prerelease` / `draft` リリースは無視。
  リポジトリにまだリリースが無い (404) 場合も「最新バージョンを
  使用しています」として扱う。
- `src/ui/UpdateAvailableDialog` — 通知ダイアログ。リリースノートは
  `QTextBrowser::setMarkdown` で表示。Update Now / Remind Me Later /
  Skip This Version の 3 択。
- `src/core/UpdateDownloader` — プラットフォーム別アセット選択
  (`macos-<arch>.dmg` / `windows-<arch>-setup.exe` /
  `linux-<arch>.AppImage`) と SHA256 検証、各 OS 向けインストール
  スクリプトの生成・起動 (macOS: hdiutil + cp、Windows: 新インス
  トーラを `/SILENT` 起動、Linux: 旧 AppImage を新ファイルで置換)。
  - **Windows のアーキ表記は CI と揃える**: `release.yml` のアセット名は
    `windows-x64-setup.exe` で固定しているため、`UpdateDownloader` 側でも
    `x64` を期待する。`QSysInfo::currentCpuArchitecture()` の戻り値
    (`x86_64`) をそのまま使うと「No matching asset」になる。
  - **Linux の "自分のパス" は `$APPIMAGE`**: AppImage 実行時は
    `QCoreApplication::applicationFilePath()` が `/tmp/.mount_xxx/...` を
    返してしまい、実体の `.AppImage` ファイルパスではない。差し替え対象を
    決めるときは環境変数 `APPIMAGE` を優先する。
- `release.yml` (CI) — `v*` タグ push でアセット + `.sha256` を
  自動添付。
- 開発ビルド (`Settings::version()` が `0.0.0` ないし `0.0.0-dev`)
  では起動時の自動チェックをスキップ。手動チェックは可能。

---

## 配布 Web サイト (GitHub Pages)

ユーザーが farman を知り・ダウンロードするための静的ランディングサイト。
リポジトリの `docs/` を **GitHub Pages** (`main` ブランチ / `/docs` フォルダ)
から配信する想定。依存ゼロの素の HTML/CSS/JS で構成する。

### 構成 (`docs/`)
- `index.html` — 日本語版 (ルート)。`en/index.html` — 英語版。
  ヘッダーの言語切替リンク (日本語 ⇆ English) で行き来する。日本語をルート、
  英語を `/en/` に置く (会社の主市場が日本のため)。
- `style.css` — 共通スタイル。`prefers-color-scheme` でライト/ダーク自動切替。
- `download.js` — **最新リリースを GitHub API から取得して OS 別ダウンロード
  ボタンを動的生成**する。アセット名にバージョンが入る
  (`farman-vX.Y.Z-macos-arm64.dmg` 等) ため固定の直リンクが作れないので、
  `releases/latest` を叩いて訪問者の OS を判定し、適切なアセットへのボタンを
  組み立てる。API 失敗時は Releases ページへのリンクにフォールバック。
  `<html lang>` を見て文言を日本語/英語で出し分ける (両ページ共用)。
- `screenshots/` — 機能スクリーンショット。`farman-icon.{png,svg}` — ロゴ。

### コンテンツ
- ヒーロー (キャッチ + OS 判定ダウンロードボタン)、主な機能グリッド、
  スクリーンショット (「ファイル操作・表示モード」「組み込みビュアー」の 2 群)、
  OS 別ダウンロード、免責事項 (現状有姿の法律用語は避け「データはバックアップを /
  作者は責任を負わない」を平易に明記)。
- **Windows ダウンロードカードに SmartScreen 注記** (未署名のため「詳細情報 →
  実行」を案内)。
- スクリーンショットは当面、日本語 UI で撮影したものを英語ページでも流用
  (英語ページに「app は英語 UI も同梱」と注記)。

### 公開フロー
- GitHub Pages は **Settings → Pages で有効化した瞬間が公開**。有効化前は
  `main` に `docs/` があってもサイトは存在しない (= マージ/ push だけでは
  自動公開されない)。有効化後は `main` への push で自動再公開。
- 独自ドメイン (`farman.mashsoft.co.jp` 等) を CNAME で当てる余地あり (将来検討)。

---

## 非機能要件

- 起動時間: 1秒以内
- 大量ファイル（10,000件以上）でもスクロールが滑らか
- ファイル操作はバックグラウンドスレッドで実行

---

## バックログ (低優先・あとで触る項目)

機能仕様ではなく、開発インフラやビルド・配布まわりの改善メモ。優先度が低いか、
着手するタイミングが先送りになっている項目をここに集約しておく。
**いずれも現行ロードマップ (v1.0 に向けた残件) が一通り片付いてから着手する想定**で、
それまでは後回しにする (2026-05-29 ユーザー判断)。

### CI / ビルド系

- **各 OS 用の並列度チューニング**。macOS / Linux は安定性のため
  `--parallel 2` に固定済み (無制限だと少メモリランナーでメモリ逼迫 → clang が
  ストールしてジョブが固まるため)。その分 macOS のビルド時間は長め (30〜40 分)。
  将来、より大きなランナー or ccache 等で速度と安定性を両立できないか検討。
- **ビルド ID 埋め込み** — 現状は `FARMAN_VERSION` マクロのみ。git short SHA 等を
  バージョン文字列に追加する案 (再現性・問い合わせ対応用)。後回し。

### 配布系

- **Universal Binary 化** (macOS arm64 + x86_64) — 保留。
  2026-05-16 の v0.9.0-test で「macos-13 (Intel) + macos-latest (arm64)
  並列ビルド + lipo 結合」方式を試したが、GitHub Actions の macos-13
  runner が割り当てられず queue に滞留したため断念。CMakeLists.txt の
  Intel Homebrew prefix 対応コードはローカル Intel ビルド用にツリーに残す。
  将来再挑戦するなら「macos-latest 1 ランナーで Universal Qt SDK +
  自前 Universal libarchive/uchardet ビルド + `CMAKE_OSX_ARCHITECTURES`
  指定」路線を検討する。
- **`release.yml`** によるタグ push → GitHub Releases 自動公開 (作成済み、
  [.github/workflows/release.yml](.github/workflows/release.yml))。`v*` タグ
  push or workflow_dispatch で 3 OS の DMG / AppImage / zip を生成し、Releases
  に **draft** として公開する (本人が UI 確認後 Publish)。
- **コード署名** — macOS は **Developer ID 署名 + 公証を実装・運用済み**
  (v0.9.5〜、`.app` / `.dmg` とも署名 + notarize + staple。詳細は「リリースと
  配布」→「コード署名 (CI)」節)。残るは **Windows Authenticode 署名** (未対応、
  当面は SmartScreen 注記で回避案内。第一候補は Azure Trusted Signing)。
  費用目安: Apple Developer $99/年、Windows OV/EV $200〜500/年。
- **Linux 配布の拡充**: AppImage + `.deb` 配布済み (`release.yml` で
  linuxdeploy の AppDir をそのまま `/opt/farman/` に詰める方式)。
  `.rpm` / Flathub / AUR は需要次第で追加検討 (現状は予定なし)。
- **macOS DMG の見た目** — 背景画像 / ボリュームアイコンの設定は後回し
  (現状は `create-dmg` のアイコン配置とウィンドウサイズ固定のみ)。
- **Windows のファイル関連付け登録** — インストーラでの拡張子関連付けは未対応。
  後回し。
- **配布サイトの独自ドメイン** — `farman.mashsoft.co.jp` 等を GitHub Pages の
  CNAME に当てる案。当面は `*.github.io` で運用し後回し。

### ドキュメント系

- **CHANGELOG.md** — 作成済み ([CHANGELOG.md](CHANGELOG.md))。Keep a Changelog
  形式でユーザー視点の主要変更点を要約。コミット単位の詳細は `release.yml`
  の `generate_release_notes` に任せる住み分け。
- **CONTRIBUTING.md** — 外部 contributor が現れた時に整備。
- **screenshot / GIF demo** を README に貼る — ある程度 UI が安定してから。

### 機能拡張系

- **プラグインの正式公開 (v1.0 以降)** — 現状プラグイン機構は内部実装として
  だけ存在し、外部公開していない (SPEC.md "プラグインシステム" 節参照)。
  v1.0 リリース後に下記の順で公開検討する:
  1. リポジトリ同梱のサンプル外部プラグインを 1〜2 件試作 (`IViewerPlugin`
     実装、CMake から別ターゲットで `.dylib` / `.so` / `.dll` 生成)。API の
     使いにくさ・抜けをまずこれで洗う。
  2. 必要なら `IViewerPlugin` を破壊的に改訂 (この段階ならまだ自由が効く)。
  3. Help → Plugins... メニュー復活 + プラグインディレクトリの設定 UI 追加。
  4. プラグイン作者向けのリファレンス文書 + テンプレートとしてサンプルを公開。

- **アーカイブ作成オプション** — 現状 `CreateArchiveDialog` はフォーマット /
  出力先 / ファイル名のみ。読み取り側の password 対応 (`ArchiveContext::password`)
  と対称になるよう、作成時の追加オプションを後日整える:
  - **パスワード (= 暗号化)**: libarchive `archive_write_set_passphrase` + 形式別
    オプション (`zip:encryption=aes256` / `zip:encryption=zipcrypto`)
  - **圧縮レベル** (0〜9): `archive_write_set_filter_option` で gzip/bzip2/xz/zip
    の deflate レベルを指定
  - **暗号化方式** (AES-256 / ZipCrypto): zip 形式のみ
  - **パス文字コード** (Shift_JIS / UTF-8): Windows 旧 zip との互換性。要件が
    出たら追加
  - **Solid 圧縮 / Volume splitting**: 7z 作成サポート自体が無いので大きく後回し
