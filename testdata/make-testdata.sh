#!/usr/bin/env bash
# farman の動作確認用テストデータを生成する。
#
#   cd testdata && ./make-testdata.sh
#
# 生成物 (archives/ compressed/ nested/ special/) は毎回作り直す。src/ が元ネタ。
# 必要なツールが無い形式は「SKIP」と表示して飛ばす (中断はしない)。
#
# 生成物も git 管理する。理由は、.rar のように生成器が手元に無いものや、壊れた
# 書庫のように「実物」でないと意味が無いものが混ざるため。対応形式を増やしたとき
# だけこのスクリプトを回して差分をコミットする運用にする。
#
# 中身は意図的に小さく保つこと (全体で 100KB 前後)。大きなファイルを入れると
# 全アーカイブがその分太り、履歴に効いてくる。進捗表示や大ファイル経路の確認に
# 大きなファイルが要るときは、git 管理外の testdata/local/ に自分で置く:
#   mkdir -p testdata/local && head -c 100m /dev/urandom > testdata/local/blob.bin
set -uo pipefail
cd "$(dirname "$0")"

# Homebrew の libarchive を優先する。macOS 同梱の bsdtar (libarchive 3.7.4) は
# zstd / lz4 コーデックと 7zip 書き出しを持たないため。
BSDTAR=/opt/homebrew/opt/libarchive/bin/bsdtar
[ -x "$BSDTAR" ] || BSDTAR=$(command -v bsdtar)

ok()   { printf '  OK    %s\n' "$1"; }
skip() { printf '  SKIP  %-24s (%s)\n' "$1" "$2"; }
have() { command -v "$1" >/dev/null 2>&1; }

rm -rf archives compressed nested special
mkdir -p archives compressed nested special

# ── 元ネタ ──────────────────────────────────
# 毎回同じ内容になるよう固定文言で作る (日時だけは実行時刻)。
echo "== src/ =="
rm -rf src && mkdir -p src/docs src/empty-dir "src/dir with space"
printf 'hello farman\n'                       > src/hello.txt
printf 'a,b,c\n1,2,3\n4,5,6\n'                > src/table.csv
printf '# Title\n\nbody text\n'               > src/readme.md
printf 'all:\n\techo build\n'                 > src/Makefile
printf 'nested content\n'                     > src/docs/note.txt
printf '日本語の内容です\n'                    > src/日本語ファイル.txt
printf 'x\n'                                  > "src/dir with space/x.txt"
ok "src/ (元ネタ)"

# ── コンテナ形式 ────────────────────────────
echo "== archives/ =="
zip -qr archives/sample.zip src && ok "sample.zip"

# パスワード付き zip (ZipCrypto)。パスワードは "farman"。
# AES-256 の zip は system zip では作れないので、farman 自身の
# 「アーカイブ作成」で作って動作確認する。
zip -qr -P farman archives/sample-password-zipcrypto.zip src \
  && ok "sample-password-zipcrypto.zip (pass: farman)"

tar -cf  archives/sample.tar     src && ok "sample.tar"
tar -czf archives/sample.tar.gz  src && ok "sample.tar.gz"
tar -czf archives/sample.tgz     src && ok "sample.tgz"
tar -cjf archives/sample.tar.bz2 src && ok "sample.tar.bz2"
tar -cjf archives/sample.tbz2    src && ok "sample.tbz2"

if have xz; then
  tar -cf - src | xz      > archives/sample.tar.xz   && ok "sample.tar.xz"
  tar -cf - src | xz      > archives/sample.txz      && ok "sample.txz"
  tar -cf - src | xz -F lzma > archives/sample.tar.lzma && ok "sample.tar.lzma"
else
  skip "sample.tar.xz / .tar.lzma" "xz が無い"
fi

if have zstd; then
  tar -cf - src | zstd -q > archives/sample.tar.zst  && ok "sample.tar.zst"
  tar -cf - src | zstd -q > archives/sample.tzst     && ok "sample.tzst"
else
  skip "sample.tar.zst" "zstd が無い"
fi

if have lz4; then
  tar -cf - src | lz4 -q  > archives/sample.tar.lz4  && ok "sample.tar.lz4"
else
  skip "sample.tar.lz4" "lz4 が無い"
fi

if have compress; then
  tar -cf - src | compress > archives/sample.tar.Z   && ok "sample.tar.Z"
  tar -cf - src | compress > archives/sample.taz     && ok "sample.taz"
else
  skip "sample.tar.Z" "compress が無い"
fi

if have lzip; then
  tar -cf - src | lzip    > archives/sample.tar.lz   && ok "sample.tar.lz"
else
  skip "sample.tar.lz" "lzip が無い (brew install lzip)"
fi

"$BSDTAR" -cf archives/sample.7z   --format=7zip src && ok "sample.7z"
"$BSDTAR" -cf archives/sample.cpio --format=cpio src && ok "sample.cpio"
"$BSDTAR" -cf archives/sample.ar   --format=ar   src/hello.txt src/table.csv \
  && ok "sample.ar"

# xar / pkg: macOS 同梱の /usr/bin/xar で作る (Homebrew libarchive は書き出し非対応)。
if have xar; then
  xar -cf archives/sample.xar src && ok "sample.xar"
  cp archives/sample.xar archives/sample.pkg && ok "sample.pkg (xar と同一内容)"
else
  skip "sample.xar / .pkg" "xar が無い"
fi

# deb: ar アーカイブに debian-binary / control.tar.gz / data.tar.gz を
# この順で入れたもの。dpkg-deb が無くても ar で組み立てられる。
(
  set -e
  work=$(mktemp -d)
  trap 'rm -rf "$work"' EXIT
  printf '2.0\n' > "$work/debian-binary"
  mkdir -p "$work/ctl"
  cat > "$work/ctl/control" <<'CONTROL'
Package: farman-testdata
Version: 1.0
Architecture: all
Maintainer: farman test <test@example.com>
Description: sample deb for farman archive tests
CONTROL
  tar -czf "$work/control.tar.gz" -C "$work/ctl" control
  tar -czf "$work/data.tar.gz" -C . src
  # macOS の ar は既定でシンボルテーブルを作ろうとし、Mach-O でないメンバーを
  # 落としてしまう (中身が __.SYMDEF だけの 96 バイトになる)。-S で抑止する。
  (cd "$work" && ar -qS sample.deb debian-binary control.tar.gz data.tar.gz 2>/dev/null)
  cp "$work/sample.deb" archives/sample.deb
) && ok "sample.deb" || skip "sample.deb" "組み立てに失敗"

have rar || skip "sample.rar" "rar は proprietary。作成不可 (入手した .rar を置いて使う)"
have lha || skip "sample.lzh" "lha が無い (brew install lha)。farman-plugin-lzh の確認用"

# ── 単一ファイル圧縮 (中身 1 エントリ) ───────
echo "== compressed/ =="
cp src/hello.txt compressed/hello.txt
gzip  -kqf compressed/hello.txt && ok "hello.txt.gz"
bzip2 -kqf compressed/hello.txt && ok "hello.txt.bz2"
have xz    && { xz    -kqf compressed/hello.txt; ok "hello.txt.xz"; } \
           || skip "hello.txt.xz" "xz が無い"
have xz    && { xz -F lzma -kqf -c compressed/hello.txt > compressed/hello.txt.lzma; ok "hello.txt.lzma"; }
have zstd  && { zstd -q -k -f compressed/hello.txt -o compressed/hello.txt.zst; ok "hello.txt.zst"; } \
           || skip "hello.txt.zst" "zstd が無い"
have lz4   && { lz4 -q -k -f compressed/hello.txt compressed/hello.txt.lz4; ok "hello.txt.lz4"; } \
           || skip "hello.txt.lz4" "lz4 が無い"
have compress && { compress -c compressed/hello.txt > compressed/hello.txt.Z; ok "hello.txt.Z"; } \
              || skip "hello.txt.Z" "compress が無い"
have lzip  && { lzip -k -f compressed/hello.txt; ok "hello.txt.lz"; } \
           || skip "hello.txt.lz" "lzip が無い (brew install lzip)"
have lzop  && { lzop -q -f -o compressed/hello.txt.lzo compressed/hello.txt; ok "hello.txt.lzo"; } \
           || skip "hello.txt.lzo" "lzop が無い (brew install lzop)"
rm -f compressed/hello.txt

# ── ネスト (アーカイブ内アーカイブ) ──────────
echo "== nested/ =="
(
  set -e
  work=$(mktemp -d)
  trap 'rm -rf "$work"' EXIT
  mkdir -p "$work/level3"
  printf 'deepest level\n' > "$work/level3/deep.txt"
  (cd "$work" && zip -qr level3.zip level3)
  mkdir -p "$work/level2" && mv "$work/level3.zip" "$work/level2/"
  printf 'middle level\n' > "$work/level2/mid.txt"
  (cd "$work" && zip -qr level2.zip level2)
  mkdir -p "$work/level1" && mv "$work/level2.zip" "$work/level1/"
  printf 'top level\n' > "$work/level1/top.txt"
  (cd "$work" && zip -qr nested-3levels.zip level1)
  cp "$work/nested-3levels.zip" nested/
) && ok "nested-3levels.zip (zip の中に zip が 2 段)"

(
  set -e
  work=$(mktemp -d)
  trap 'rm -rf "$work"' EXIT
  mkdir -p "$work/inner"
  printf 'inside tar.gz\n' > "$work/inner/inner.txt"
  tar -czf "$work/inner.tar.gz" -C "$work" inner
  (cd "$work" && zip -qr mixed-zip-in-targz.zip inner.tar.gz)
  cp "$work/mixed-zip-in-targz.zip" nested/
) && ok "mixed-zip-in-targz.zip (zip の中に tar.gz)"

# ── 特殊ケース ──────────────────────────────
echo "== special/ =="
# CP932 (Shift_JIS) のファイル名を持つ zip。UTF-8 フラグを立てないので、
# 「ファイル名の文字コード」設定と自動判別の確認に使う。
python3 - <<'PY' && ok "special/cp932-names.zip (Shift_JIS 名・UTF-8 フラグ無し)"
import zipfile
# zipfile はファイル名を cp437 で書ける場合 UTF-8 フラグを立てない。
# cp932 のバイト列を cp437 として解釈した文字列を渡すと、生バイトがそのまま入る。
def raw(name_cp932: str) -> str:
    return name_cp932.encode('cp932').decode('cp437')
with zipfile.ZipFile('special/cp932-names.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr(raw('日本語ファイル.txt'), 'CP932 のファイル名です\n'.encode('utf-8'))
    z.writestr(raw('フォルダ/中身.txt'), 'nested cp932 name\n'.encode('utf-8'))
PY

# 拡張子の無いファイル / 複合拡張子 (ファイルパターン設定の確認用)
cp src/Makefile           special/Makefile          && ok "special/Makefile (拡張子無し)"
printf 'node_modules/\n'> special/.gitignore        && ok "special/.gitignore"
tar -czf special/archive.backup.tar.gz src         && ok "special/archive.backup.tar.gz (複合拡張子)"
printf 'not really a zip\n' > special/broken.zip    && ok "special/broken.zip (壊れた書庫)"
: > special/empty.zip                               && ok "special/empty.zip (0 バイト)"

echo
echo "完了: $(pwd)"
