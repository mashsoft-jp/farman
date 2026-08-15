# farman テストデータ

farman の動作確認に使うサンプルファイル置き場。

- 再生成: `./make-testdata.sh`（`archives/` `compressed/` `nested/` `special/` `src/` を作り直す）
- 生成に必要なツールが無い形式は `SKIP` と表示して飛ばす（途中で止まらない）
- **生成物も git 管理する。** `.rar` のように生成器が手元に無いものや、壊れた書庫の
  ように「実物」でないと意味が無いものが混ざるため。対応形式を増やしたときだけ
  スクリプトを回して差分をコミットする
- リリースの配布物には含めない（`.gitattributes` の `export-ignore`）

## 大きさについて

全体で 200KB 程度に収まるよう、意図的に小さいファイルだけで作っている。大きな
ファイルを 1 つ入れると全アーカイブがその分太り、履歴に効いてくるため。

進捗ダイアログや大ファイル経路の確認に大きなファイルが必要なときは、git 管理外の
`testdata/local/` に自分で置く:

```bash
mkdir -p testdata/local && head -c 100m /dev/urandom > testdata/local/blob.bin
```

## 中身

### `src/` — アーカイブの元ネタ
`hello.txt` / `table.csv` / `readme.md` / `Makefile` / `docs/note.txt` /
`日本語ファイル.txt` / `dir with space/x.txt` / `empty-dir/`。

日本語名・空白入りディレクトリ名・拡張子無しファイル・空ディレクトリを意図的に
混ぜてある。

### `archives/` — コンテナ形式

| ファイル | 備考 |
|---|---|
| `sample.zip` | |
| `sample-password-zipcrypto.zip` | パスワード `farman`（ZipCrypto） |
| `sample.tar` `sample.tar.gz` `sample.tgz` | |
| `sample.tar.bz2` `sample.tbz2` | |
| `sample.tar.xz` `sample.txz` `sample.tar.lzma` | |
| `sample.tar.zst` `sample.tzst` | |
| `sample.tar.lz4` | |
| `sample.tar.Z` `sample.taz` | |
| `sample.7z` | |
| `sample.cpio` | |
| `sample.ar` | |
| `sample.deb` | `debian-binary` + `control.tar.gz` + `data.tar.gz` |
| `sample.xar` `sample.pkg` | 同一内容 |

### `compressed/` — 単一ファイル圧縮（中身 1 エントリ）
`hello.txt.gz` `.bz2` `.xz` `.lzma` `.zst` `.lz4` `.Z`

### `nested/` — アーカイブ内アーカイブ
- `nested-3levels.zip` — zip の中に zip が 2 段（`level1/level2.zip` → `level2/level3.zip`）
- `mixed-zip-in-targz.zip` — zip の中に tar.gz

### `special/` — 特殊ケース

| ファイル | 何の確認用か |
|---|---|
| `cp932-names.zip` | ファイル名が CP932（UTF-8 フラグ無し）。「ファイル名の文字コード」設定と自動判別 |
| `Makefile` | 拡張子を持たないファイル。ファイルパターン `Makefile` の確認 |
| `.gitignore` | 同上（ドット始まり） |
| `archive.backup.tar.gz` | 複合拡張子。パターン `*.tar.gz` の確認 |
| `broken.zip` | 中身が zip でない。エラー処理の確認 |
| `empty.zip` | 0 バイト。エラー処理の確認 |

## 用意できていない形式

| 形式 | 理由 / 用意する方法 |
|---|---|
| `.rar` | RAR の作成器は proprietary。入手した `.rar` を `archives/` に置いて使う |
| `.lzh` | `brew install lha` が必要（farman-plugin-lzh の確認用） |
| `.lz`（lzip） | `brew install lzip` |
| `.lzo` | `brew install lzop` |
| `.iso` | 今回は対象外。必要なら `bsdtar -cf x.iso --format=iso9660 src` で作れる |
| AES-256 の zip | system の `zip` は ZipCrypto のみ。farman 自身の「アーカイブ作成」で作って確認する |

## メモ

- 生成には Homebrew の libarchive（`/opt/homebrew/opt/libarchive/bin/bsdtar`）を使う。
  macOS 同梱の bsdtar は libarchive 3.7.4 で、zstd / lz4 コーデックと 7zip の
  書き出しを持たないため。
- macOS の `ar` は既定でシンボルテーブルを作ろうとし、Mach-O でないメンバーを
  落としてしまう（96 バイトの壊れた `.deb` になる）。`ar -qS` で抑止している。
- `xar` は Homebrew libarchive が書き出し非対応なので、macOS 同梱の `/usr/bin/xar` を使う。
