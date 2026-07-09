#pragma once

#include <QString>

namespace Farman::ArchivePath {

// "/abs/path/to/x.zip!/inner/dir" 形式を扱うユーティリティ。
// 通常 FS では使われない `!` を区切りに、アーカイブ内仮想 FS を表現する。

// 拡張子だけでアーカイブ判定 (実在チェックはしない)。
// 対応: .zip / .tar / .tar.gz / .tgz / .tar.bz2 / .tbz2 / .tar.xz / .txz
// これに加え、registerArchiveExtension() で登録された拡張子 (アーカイブプラグイン
// が名乗るもの、例 .lzh) も合致する。
bool isArchiveExtension(const QString& path);

// アーカイブプラグインが扱う拡張子を実行時に登録する。dotExt は ".lzh" のように
// 先頭ドット付きで渡す (無くても補う)。main() が ArchiveDispatcher からロードした
// プラグインの拡張子を起動時に流し込む。utils 層がコア (ArchiveDispatcher) に
// 依存しないよう、逆に main 側から注入する形をとる。
void registerArchiveExtension(const QString& dotExt);

// ファイル名からアーカイブ拡張子 (組み込み + registerArchiveExtension で登録した
// プラグイン拡張子) を剥がしたベース名を返す。展開時の既定フォルダ名などに使う。
// 最長一致を優先する (例 "x.tar.gz" → "x"、"x.lzh" → "x")。合致しなければ元の
// fileName をそのまま返す。fileName はパスではなくファイル名部分を渡すこと。
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
Split splitArchivePath(const QString& path);

// archive + inner → "x.zip!/inner"。
//   inner == "" / "/" の場合は "x.zip!/" を返す。
//   inner が "/" で始まらない場合は補う。
QString joinArchivePath(const QString& archivePath, const QString& innerPath);

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
