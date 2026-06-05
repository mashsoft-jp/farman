# farman への貢献ガイド

farman に興味を持っていただきありがとうございます。バグ報告 / 機能要望 /
PR どれも歓迎です。本ドキュメントは外部 contributor 向けに「迷ったらここ
を見れば最短で動ける」よう、開発フローを最小限まとめたものです。

仕様や実装方針の詳細は [SPEC.md](SPEC.md) / [ARCHITECTURE.md](ARCHITECTURE.md)
を参照してください。

---

## 開発環境

### 前提条件

- **Qt 6** (Core / Widgets / Core5Compat / Concurrent / Network / LinguistTools)
- **C++20** 対応コンパイラ (clang 14+ / gcc 11+ / MSVC 2022)
- **CMake** 3.21 以上
- **libarchive** / **uchardet** (テキスト/アーカイブ機能で使用)

### OS 別セットアップ

| OS | 依存導入 |
|---|---|
| macOS | `brew install qt libarchive uchardet pkg-config` |
| Linux (Ubuntu/Debian) | `apt install qt6-base-dev libqt6core5compat6-dev libarchive-dev libuchardet-dev pkg-config libgl1-mesa-dev libxkbcommon-dev` |
| Windows | Qt 6 を公式オンラインインストーラから、libarchive / uchardet は vcpkg (`vcpkg install libarchive:x64-windows uchardet:x64-windows`) |

### ビルド

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt   # macOS の例
cmake --build build
```

詳細は [README.md](README.md) の「ビルド方法」セクション参照。

---

## ブランチモデル

```
feature/* / fix/*  →  release/vX.Y.Z  →  main
(個別作業)            (次リリース集約)    (リリース可能)
```

| ブランチ | 直接 push | PR ベース | 役割 |
|---|---|---|---|
| `main` | × | ○ (release / hotfix から) | 常にリリース可能な安定ブランチ。直接作業しない |
| `release/vX.Y.Z` | × | ○ (feature / fix から) | 次リリースに入れる変更の集約・最終確認 |
| `feature/*` | ○ | - | 新機能や仕様変更の個別作業 |
| `fix/*` | ○ | - | 通常のバグ修正 |
| `hotfix/*` | ○ | - | 公開済み版への緊急修正。対象タグまたは main から切る |

`develop` / `staging` は使いません。複数の変更を次バージョンにまとめる場合は
`release/vX.Y.Z` を作り、個別作業ブランチから取り込みます。

公開済みバージョンは `vX.Y.Z` タグで固定します。正式リリース後は
`release/vX.Y.Z` を `main` にマージし、main の HEAD に正式タグを打ちます。

---

## PR の作り方

1. リポジトリを fork して、対象の `release/vX.Y.Z` または `main` から作業ブランチを切る
   ```bash
   git checkout release/v0.9.6
   git pull
   git checkout -b feature/your-topic
   ```
2. 変更を加える (コードスタイル下参照)
3. ローカルでビルドが通ることを確認
   ```bash
   cmake --build build
   ./build/farman.app/Contents/MacOS/farman   # macOS の例
   ```
4. コミット (粒度は「1 機能 / 1 修正」単位、 Co-Authored-By は任意)
5. push → GitHub で対象の `release/vX.Y.Z` へ向けて PR を作成
6. CI (`build.yml`) が 3 OS で通ることを確認。落ちたら修正コミットを積む
7. レビュー対応の後にマージ

### CI が見るもの

- `build.yml`: 3 OS でビルド成功すること (テストは現状無し)
- 失敗時は Actions タブで該当ジョブのログを確認

---

## コードスタイル

- **インデント**: スペース 2 つ (タブ不可)
- **文字エンコーディング**: UTF-8
- **C++**: C++20 (`-std=c++20`)
- **Qt のメタオブジェクト系**: `AUTOMOC` / `AUTORCC` / `AUTOUIC` が有効なので、
  `Q_OBJECT` を持つクラスは `.cpp` だけ CMakeLists.txt に書けば moc が走る
- **命名**:
  - クラス: PascalCase (`FileListModel`)
  - メンバ: `m_camelCase` (`m_currentPath`)
  - 関数: camelCase (`currentPath()`)
  - 定数 / enum: PascalCase or UPPER_CASE (混在許容、文脈で読みやすい方)
- **`namespace Farman { ... }` で全コードを囲う**
- **コメント**:
  - 「なぜ」を書く。コードから自明な「何を」は書かない
  - 制約・前提・落とし穴・「ここを変えると別の場所が壊れる」を明示するのは歓迎
  - 日本語コメント OK (プロジェクトの主な言語が日本語のため)

### 既存コードのスタイルを優先

迷ったら近隣ファイルのスタイルに合わせてください。フォーマッタは現状未導入
(`.clang-format` 未配置)。

---

## コミットメッセージ

特定のフォーマットは強制しません。以下を守れれば OK:

- 1 行目: 50〜72 文字、命令形 (例: `Fix Zip Slip in archive extraction`)
- 2 行目: 空行
- 3 行目以降: 「なぜこの変更が必要か」「どう影響するか」を日本語/英語自由に

最近のコミットログ (`git log --oneline -30`) を見て雰囲気を掴むのが
最短です。

---

## バグ報告 / 機能要望

[GitHub Issues](https://github.com/ms-haraki/farman/issues) に立ててください。

### バグ報告に欲しい情報

- OS とバージョン (macOS 14.5 / Windows 11 / Ubuntu 24.04 等)
- farman のバージョン (タイトルバー or `Help → About farman...`)
- 再現手順 (できれば最小ケース)
- 期待する挙動 / 実際の挙動
- ログがあれば添付 (ログペイン / `~/Library/Logs/farman/*.log` 等)

### 機能要望に欲しい情報

- ユースケース (どんなときに使いたいか)
- 既存機能で代用できないか検討した内容 (代用できるなら別アプローチで解決
  できる可能性がある)
- 仕様の素案があれば SPEC.md の該当節に追記する PR でも OK

---

## 翻訳

翻訳ファイルは `translations/farman_ja.ts` (日本語)。

新しい UI 文字列を加えた場合:

```bash
cmake --build build --target update_translations
```

これで `.ts` が更新される。日本語翻訳を埋めて PR に含めてください。

新規言語追加は `CMakeLists.txt` の `qt_add_translations` の `TS_FILES` に
`farman_<lang>.ts` を追加します (例: `farman_en.ts` / `farman_zh.ts`)。

---

## ライセンス

farman は [MIT License](LICENSE) で公開されています。PR を送ることで、
あなたの変更も MIT License の下でリリースされることに同意したものと
みなします。

---

## 気軽に Issue / Discussion を立ててください

- 大きめの変更を予定しているときは PR の前に Issue で相談すると、
  方針が合うかどうかを早めにすり合わせできます
- 単純な質問も Issue で OK (テンプレ未整備につき自由に書いてください)

ありがとうございます!
