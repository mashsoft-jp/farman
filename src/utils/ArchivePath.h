#pragma once

#include <QString>
#include <QStringList>

namespace Farman::ArchivePath {

// "/abs/path/to/x.zip!/inner/dir" 形式を扱うユーティリティ。
// 通常 FS では使われない `!` を区切りに、アーカイブ内仮想 FS を表現する。

// ファイル名だけでアーカイブ判定 (実在チェックはしない)。
// 判定は setArchivePatterns() で流し込まれた「現在有効な形式のファイル名 glob」
// に対して行う。未注入のときは組み込み既定 (.zip / .tar / .tar.gz / .tgz /
// .tar.bz2 / .tbz2 / .tar.xz / .txz) にフォールバックする。
bool isArchiveExtension(const QString& path);

// 現在有効なアーカイブ形式のファイル名 glob パターン ("*.tar.gz" 形式) を
// 流し込む。組み込み形式・プラグイン形式・ユーザーが設定で上書きした拡張子を
// 解決した結果を、core 側の ArchiveFormatCatalog が起動時と設定変更時に渡す。
// utils 層がコア (ArchiveDispatcher / Settings) に依存しないよう、逆に呼び出し側
// から注入する形をとる。空リストを渡すと組み込み既定に戻る。
void setArchivePatterns(const QStringList& globPatterns);

// 現在有効なパターン (setArchivePatterns() 未注入なら組み込み既定) を返す。
QStringList archivePatterns();

// ファイル名からアーカイブ拡張子を剥がしたベース名を返す。展開時の既定フォルダ名
// などに使う。最長一致を優先する (例 "x.tar.gz" → "x"、"x.lzh" → "x")。
// 剥がせるのは "*.tar.gz" のように `*` 1 個 + 固定文字列という形のパターンだけで、
// 途中にワイルドカードを含むパターンは対象外。合致しなければ元の fileName を
// そのまま返す。fileName はパスではなくファイル名部分を渡すこと。
QString archiveBaseName(const QString& fileName);

struct Split {
  // archivePath: アーカイブ自体のローカル FS 絶対パス
  // innerPath:   アーカイブ内の絶対パス (先頭 "/" 必須、ルートは "/")
  // valid:       true なら archivePath / innerPath が埋まっている
  QString archivePath;
  QString innerPath;
  bool    valid = false;
};

// "x.zip!/inner" をパース。`!` の手前が isArchiveExtension に合致しなければ valid=false。
//
// 入れ子アーカイブ ("a.zip!/d/inner.zip!/sub") では **最内 1 段** を切り出す:
//   archivePath = "a.zip!/d/inner.zip" (それ自体が入れ子の指定)
//   innerPath   = "/sub"
// つまり archivePath は「実 FS 上のパス」とは限らない。libarchive に渡す実体が
// 要る場面では ArchiveContext::readPath() を使うこと。
Split splitArchivePath(const QString& path);

// 入れ子アーカイブのパスを段ごとに分解した結果。
//   "/abs/a.zip!/d/inner.zip!/sub" →
//     rootPath      = "/abs/a.zip"     (実 FS 上のアーカイブ)
//     innerArchives = ["d/inner.zip"]  (外側から順、先頭 '/' なし)
//     innerPath     = "/sub"           (最内アーカイブ内の現在位置)
struct NestedSplit {
  QString     rootPath;
  QStringList innerArchives;
  QString     innerPath;
  bool        valid = false;
};

// アーカイブパスを段ごとに分解する。アーカイブパスでなければ valid=false。
NestedSplit splitNestedArchivePath(const QString& path);

// 入れ子の深さ。通常 FS のパスと 1 段だけのアーカイブパスは 0、
// アーカイブの中のアーカイブに入っていれば 1、そのまた中なら 2。
// 設定「ネスト段数上限」と同じ数え方 (= アーカイブ内アーカイブを何回開いたか)。
int archiveNestingLevel(const QString& path);

// archive + inner → "x.zip!/inner"。
//   inner == "" / "/" の場合は "x.zip!/" を返す。
//   inner が "/" で始まらない場合は補う。
QString joinArchivePath(const QString& archivePath, const QString& innerPath);

// 表示用のパス文字列。アーカイブの区切り `!` を " > " に置き換え、各段の
// 先頭 `/` を落とす。アーカイブパスでなければそのまま返す。
//   "/abs/a.zip!/"                    → "/abs/a.zip"
//   "/abs/a.zip!/level1"              → "/abs/a.zip > level1"
//   "/abs/a.zip!/d/in.zip!/sub/x.txt" → "/abs/a.zip > d/in.zip > sub/x.txt"
//
// **表示専用**。ここから元のパスは復元できないので、コピー・ナビゲーション・
// ログには使わないこと。
QString displayPath(const QString& path);

// アーカイブ内パスのうち親側を返す。
//   "/foo/bar" → "/foo"
//   "/foo"     → "/"
//   "/"        → "/"
QString parentInnerPath(const QString& innerPath);

// アーカイブ内パスのファイル名部分。
//   "/foo/bar.txt" → "bar.txt"
//   "/"            → ""
QString innerBaseName(const QString& innerPath);

// アーカイブ内エントリ名を安全に展開先ディレクトリへ結合する (Zip Slip 対策)。
// 受け入れる entryName は「outputDir を基準とした相対パス」とみなし、以下を拒否:
//   - 空文字、絶対パス (Unix `/...`, Windows `C:\...` / UNC `\\server\...`)
//   - `..` / `.` セグメント
//   - 結合・正規化した結果が outputDir 配下に収まらないもの
// 受け入れた場合は cleanPath 済みの絶対パス (outputDir 配下) を返す。
// 拒否時は空 QString を返す。呼び出し側は空チェックで skip する。
QString safeJoinExtractPath(const QString& outputDir, const QString& entryName);

} // namespace Farman::ArchivePath
