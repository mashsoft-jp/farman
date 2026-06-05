# ディレクトリ比較 設計メモ

SPEC.md「ディレクトリ比較」(SPEC.md:1125〜) の実装に向けた設計案。
左右ペインの内容を突き合わせて **差分を着色表示**し、必要なものを
コピー / 同期するモード。

---

## 設計方針

### モードの位置づけ

- **比較モード**は左右ペインに **跨る** 状態。一方のペインだけが比較中という
  状態は存在しない（必ず左右セット）。
- 開始: 左右ペインのカレントディレクトリを起点に `DirectoryCompareDialog` で
  粒度と再帰を指定して走らせる。
- 終了: メニュー / ステータスバー / `Esc` （比較モード中限定）でクリア。
- ペインがディレクトリを切り替えた瞬間に **比較モードは自動解除**
  （比較対象がズレるため）。ログに記録。

### 比較結果のデータ構造

```cpp
enum class DiffStatus {
  Same,        // 反対側と一致
  Differ,      // 両側にあるが内容が違う（サイズ / mtime / hash の判定による）
  OnlyHere,    // このペインにしかない
};
```

注: 「反対側にしかない (OnlyOther)」は、このペインのファイルリストに
そもそも表示されないので、各ペインのモデルが持つ map には現れない。
「両方にしかない」アイテムも当然存在しないので、上記 3 状態で十分。

```cpp
struct CompareEntry {
  QString    name;        // ファイル名（ディレクトリ相対）
  DiffStatus status;
  bool       isDir;       // 集約結果がディレクトリか
};
```

各ペインに対して `QHash<QString, DiffStatus> overlay` を持たせる
（キー = ファイル名）。

---

## クラス構成（新規 / 変更）

### 新規

- `src/core/DirectoryCompare.{h,cpp}`
  - `enum class DiffStatus`、`enum class CompareGranularity { NameOnly, SizeMtime }`、
    `struct CompareOptions`、`struct CompareResult` 等の型定義。

- `src/core/workers/DirectoryCompareWorker.{h,cpp}`
  - 入力: `leftDir`, `rightDir`, `CompareOptions`
  - 出力: 左右それぞれの `QHash<QString, DiffStatus>`

- `src/ui/DirectoryCompareDialog.{h,cpp}`
  - 比較の **設定**（粒度・再帰）と **開始ボタン**
  - 比較中・完了後は **同期操作ボタン群** をパネル表示
    - 「Copy unique left → right」「Copy unique right → left」
    - 「Copy newer left → right (differ only)」「Copy newer right → left」
    - 「Delete unique left」「Delete unique right」（個別確認必須）
  - ボタンは `CommandRegistry::execute()` 経由でコマンドを発火し、
    既存の CopyWorker / RemoveWorker に流す

### 既存への変更

- **`src/model/FileListModel.{h,cpp}`**
  - メンバ追加:
    ```cpp
    bool                       m_compareMode = false;
    QHash<QString, DiffStatus> m_compareOverlay;  // name → status
    ```
  - API 追加: `void setCompareOverlay(QHash<QString, DiffStatus>)`、
    `void clearCompareOverlay()`
  - `data(BackgroundRole)` で `m_compareMode` のとき overlay を参照して
    色を返す（Settings の Appearance/Compare グループから取得）

- **`src/ui/FileListDelegate.{h,cpp}`**
  - 既存の `Cursor` 描画と比較色の優先順位を整理:
    `Selected > Cursor > Compare > Hidden/Dir/Normal`

- **`src/ui/FileManagerPanel.{h,cpp}`**
  - メンバ追加:
    ```cpp
    bool             m_compareMode = false;
    CompareOptions   m_compareOptions;
    ```
  - `startCompare(CompareOptions)` / `stopCompare()` を実装
  - `navigatePane()` の中で `m_compareMode == true` ならば
    `stopCompare()` を呼んで自動解除（ログ出力）
  - `View メニュー` に「Compare Directories...」「Stop Compare」を追加
  - ステータスバーへの追加文字列（「Compare: SizeMtime · Recursive」）

- **`src/keybinding/commands/`**
  - `view.compare_directories` コマンド追加（既定キー未割当、Settings で変更可）
  - `view.stop_compare` コマンド追加（既定キー未割当）

- **`src/settings/ColorScheme.{h,cpp}` / `src/settings/Settings.{h,cpp}` / `src/ui/AppearanceTab.{h,cpp}`**
  - **比較カラーは `ColorScheme` 構造体のフィールド**として追加する。
    既存のテーマ機構は Light / Dark の 2 セットを `Settings` が保持して
    `AppearanceTab::editingSide()` で切替編集する作りなので、ここに乗せると
    **Light / Dark で自動的に別の値**を持ち、テーマプリセット
    (Solarized 等) のインポート / エクスポートにも自然に含まれる。
  - 追加フィールド:
    ```cpp
    // ── ディレクトリ比較 ───────────────────────
    QColor compareDifferFg, compareDifferBg;
    QColor compareOnlyHereFg, compareOnlyHereBg;
    // Same は専用色を持たず、通常の色を使う
    ```
  - `defaultLightScheme()` / `defaultDarkScheme()` でテーマ毎の既定値を設定
    （Light: Differ=暗オレンジ on 明黄, OnlyHere=暗緑 on 明緑 など）
  - `colorSchemeToJson` / `colorSchemeFromJson` の 4 フィールドを追加
  - Appearance タブ → **Compare グループ** 追加:
    - Differ:   Foreground / Background
    - OnlyHere: Foreground / Background
    - 上 2 グループ (Address / Cursor / File List) と同じレイアウト規約に従う

---

## 比較ワーカーの動き

### NameOnly

1. `QDir::entryInfoList(NoDotAndDotDot)` で左右の一覧を取得
2. 左の set と右の set の差分を取り、左右の overlay を構築
3. 共通項はすべて `Same`

ほぼ瞬時。

### SizeMtime（既定）

1. NameOnly と同じ手順
2. 共通項について `size` と `mtime` を比較
   - 一致 → `Same`
   - 不一致 → `Differ`
3. mtime の比較は秒精度（FS によってナノ秒以下が落ちる）

ほぼ瞬時。

### 再帰モード

- 同名サブディレクトリ同士を再帰的に比較
- ディレクトリ自体の `DiffStatus`:
  - 中身が全て `Same` → `Same`
  - 1 つでも `Differ` または `OnlyHere` → `Differ`
  - 反対側にディレクトリ自体が無い → `OnlyHere`

再帰ありの場合でも、表示する overlay は直下エントリの Same/Differ に集約する。

---

## 着色の優先順位

`FileListDelegate::paint()` での Background 決定:

```
1. Selected      (赤紫系の既存色)        ← 既存
2. Cursor        (オレンジ系の既存色)    ← 既存
3. CompareDiffer (Settings の Differ.bg) ← 新規 (m_compareMode 時のみ)
4. CompareOnlyHere (Settings の OnlyHere.bg) ← 新規 (m_compareMode 時のみ)
5. Hidden / Dir / Normal                 ← 既存
```

Foreground も同様の優先順位。

注: Selected が比較色より優先される理由は、選択は **ユーザーが今操作しようと
しているもの** であり、視覚的に隠れたら困るため。

---

## 同期操作

`DirectoryCompareDialog` を **比較開始後はパネルとして開いたまま** にし、
ボタンで同期操作を実行する設計。閉じても比較モードは続く（ダイアログは
View メニューから再表示可能）。

ボタンは内部的に既存 CopyWorker / RemoveWorker を呼ぶ:

| ボタン | 動作 |
|---|---|
| Copy unique L→R | 左にしかないものを右にコピー |
| Copy unique R→L | 右にしかないものを左にコピー |
| Copy newer L→R  | Differ 行のうち左の mtime が新しいものを右に上書きコピー |
| Copy newer R→L  | 同上の逆方向 |
| Delete unique L | 左にしかないものを左から削除（確認必須） |
| Delete unique R | 右にしかないものを右から削除（確認必須） |

完了後、自動で **比較を再実行** して overlay を更新する。

---

## 段階的実装

### フェーズ A — MVP: 表示のみ

1. `DirectoryCompare.{h,cpp}` の型
2. `DirectoryCompareWorker` (NameOnly / SizeMtime のみ、非再帰)
3. `DirectoryCompareDialog`（粒度 + 再帰 + 開始ボタン、同期操作は無し）
4. `FileListModel` の比較オーバーレイ機構
5. `FileManagerPanel::startCompare()` / `stopCompare()`
6. ステータスバー表示
7. `view.compare_directories` / `view.stop_compare` コマンド
8. View メニュー追加
9. Appearance タブ → Compare グループ
10. `navigatePane` での自動解除

ここまでで「F12 のような何か → ダイアログ → OK で左右に色が乗る → 移動で解除」
が一通り動く。

### フェーズ B — 再帰

11. 再帰比較とディレクトリ集約
12. 再帰モードのときの「ディレクトリのクリックで掘れるか / 比較解除か」を整理
    - **採用**: 掘ると自動解除（通常の `navigatePane` 経路に乗る）

### フェーズ C — 同期操作

13. `DirectoryCompareDialog` に同期操作ボタンを追加
14. 既存 CopyWorker / RemoveWorker への流し込み
15. 同期完了 → 再比較トリガー
16. ログ出力 (`Compare sync: copied N items L → R`)

### フェーズ D — 拡張

17. 結果サマリのログ (`Compare done: +N / -M / =K / Δ L`)
18. CSV / JSON エクスポート（オプションメニュー）
19. 単発の Enter（カーソル行のみコピー）

---

## アーカイブ内ブラウジングとの相互排他

ディレクトリ比較とアーカイブ内ブラウジングは **同時には動かさない**。

- **比較モード中に Enter でアーカイブに潜る**:
  `navigatePane()` が走るので、設計通り比較モードが自動解除される。アーカイブを
  抜けて元の FS に戻っても、比較モードは復元しない（ユーザーが明示再開）。
- **アーカイブ内にいる状態で「Compare Directories」発動**:
  比較ワーカーは `QDir::entryInfoList` 前提なので動かない。発動時に
  `pane->model()->isInArchiveMode()` でガードし、左右どちらかがアーカイブ
  モードなら警告ログ + メッセージで中止する。
- **メニュー項目の enable 状態**:
  `View → Compare Directories...` は両ペインともアーカイブモードでない時のみ
  enabled。アーカイブモード切替時 (`navigatePane` の完了後) にメニュー状態を
  更新する。
- 「両ペインともアーカイブ内」での比較は、比較ワーカーが `ArchiveContext`
  経由のエントリ列挙パスを別途持つ必要があり実装コスト大。v1.0 では未対応。

## スコープ外（v1.0）

- **ファイル単位の Enter で当該行のみ転送** … ファイラのキーバインド全体に
  影響するためフェーズ D。
- **着色のフィルタ追従** … SPEC.md:1138 に従って **自動追従**。比較結果は
  ファイル名キーなので、ソート・フィルタが変わっても overlay は再評価不要。
- **比較結果のキャッシュ永続化** … 比較は毎回新規（ディレクトリ移動で消える）。

---

## 主な意思決定の根拠

- **比較モードはペインに跨る状態**: 片方だけ比較中という状態を持たせると、
  色の意味がユーザーに分かりにくくなる。
- **Overlay 方式（ファイル名キーの map）**: ソート・フィルタの結果と
  独立して着色できる。FileListModel への変更も最小。
- **ディレクトリ移動で自動解除**: 据え置きにしておくと、ユーザーが
  「色は何の色だっけ」と混乱しやすい。Sync Browse の自動解除と同じ
  ポリシー。
- **同期操作は既存 CopyWorker / RemoveWorker に集約**: 進捗ダイアログや
  上書き処理を再実装しない。
- **SizeMtime を既定に固定**: 日常的な同期確認では十分実用的で、重い厳密比較用の
  進捗 UI やキャンセル制御を持ち込まずに済む。
- **比較カラーを `ColorScheme` に乗せる**: 既存のテーマ機構 (Light/Dark 2 セット
  + テーマプリセットの import/export) にそのまま乗るため、追加コストがほぼ無く
  整合性も保てる。
