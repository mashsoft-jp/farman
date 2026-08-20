#include "ArchivePath.h"
#include "MediaMatchers.h"
#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace Farman::ArchivePath {

namespace {

// setArchivePatterns() 未注入のときのフォールバック。
// v0.9.9 まで組み込みで認識していた形式と同じ内容で、単体テストや、まだカタログ
// を流し込んでいない起動シーケンスの途中でも従来どおり動くようにするためのもの。
const QStringList& defaultPatterns() {
  static const QStringList pats = {
    QStringLiteral("*.tar.gz"), QStringLiteral("*.tar.bz2"), QStringLiteral("*.tar.xz"),
    QStringLiteral("*.tgz"),    QStringLiteral("*.tbz2"),    QStringLiteral("*.txz"),
    QStringLiteral("*.tar"),    QStringLiteral("*.zip"),
  };
  return pats;
}

// core 側 (ArchiveFormatCatalog) が注入した、現在有効な形式のパターン。
// 起動時に一度、以後は設定変更のたびに差し替えられる。
QStringList& injectedPatterns() {
  static QStringList pats;
  return pats;
}

// "*.tar.gz" のように「先頭の `*` + 固定文字列」という形のパターンから、
// 剥がせる固定部分 (".tar.gz") を取り出す。途中にワイルドカードを含むものや
// `*` で始まらないものは剥がしようがないので空を返す。
QString strippableSuffix(const QString& pattern) {
  if (!pattern.startsWith(QLatin1Char('*'))) return {};
  const QString tail = pattern.mid(1);
  if (tail.isEmpty()) return {};
  if (tail.contains(QLatin1Char('*')) || tail.contains(QLatin1Char('?'))) return {};
  return tail;
}

} // namespace

void setArchivePatterns(const QStringList& globPatterns) {
  injectedPatterns() = globPatterns;
}

QStringList archivePatterns() {
  return injectedPatterns().isEmpty() ? defaultPatterns() : injectedPatterns();
}

bool isArchiveExtension(const QString& path) {
  // パターンはファイル名全体に対する glob なので、ディレクトリ部分を落として
  // から照合する (パス中の `.zip` ディレクトリ名などに引っかからないように)。
  const QString fileName = QFileInfo(path).fileName();
  if (fileName.isEmpty()) return false;
  return MediaMatchers::globMatches(archivePatterns(), fileName);
}

QString archiveBaseName(const QString& fileName) {
  // 有効な全パターンのうち、末尾に一致する最長の固定サフィックスを剥がす
  // (".tar.gz" を ".tar" より優先させるため、最長一致を採用する)。
  QString best;
  for (const QString& pattern : archivePatterns()) {
    const QString suffix = strippableSuffix(pattern);
    if (suffix.isEmpty()) continue;
    if (fileName.endsWith(suffix, Qt::CaseInsensitive) && suffix.size() > best.size()) {
      best = suffix;
    }
  }

  if (best.isEmpty() || best.size() >= fileName.size()) return fileName;
  QString baseName = fileName;
  baseName.chop(best.size());
  return baseName;
}

Split splitArchivePath(const QString& path) {
  Split s;
  // 入れ子アーカイブ ("a.zip!/d/inner.zip!/sub") があるので、区切りは
  // **最も後ろ**の `!` から探す。これで archivePath 側に外側の段が丸ごと
  // 残り、innerPath は必ず「最内アーカイブの中のパス」になる。
  //
  // 後ろから順に試すのは、アーカイブ内エントリ名に `!` が含まれる場合
  // ("a.zip!/foo!bar.txt") に、その `!` を区切りと誤認しないため。左側が
  // アーカイブ名として成立する位置が見つかった時点で確定する。
  int sep = -1;
  for (int i = path.lastIndexOf(QLatin1Char('!')); i >= 0;
       i = path.lastIndexOf(QLatin1Char('!'), i - 1)) {
    if (isArchiveExtension(path.left(i))) {
      sep = i;
      break;
    }
  }
  if (sep < 0) {
    s.archivePath = path;
    return s;  // valid=false (`!` が無い / あっても拡張子が違うので通常パス扱い)
  }
  const QString archive = path.left(sep);
  QString inner = path.mid(sep + 1);
  if (inner.isEmpty() || !inner.startsWith(QLatin1Char('/'))) {
    inner = QStringLiteral("/") + inner;
  }
  // 末尾の '/' を削る (ルート "/" は維持)
  while (inner.size() > 1 && inner.endsWith(QLatin1Char('/'))) {
    inner.chop(1);
  }
  s.archivePath = archive;
  s.innerPath   = inner;
  s.valid       = true;
  return s;
}

QString joinArchivePath(const QString& archivePath, const QString& innerPath) {
  QString inner = innerPath;
  if (inner.isEmpty() || !inner.startsWith(QLatin1Char('/'))) {
    inner = QStringLiteral("/") + inner;
  }
  return archivePath + QLatin1Char('!') + inner;
}

NestedSplit splitNestedArchivePath(const QString& path) {
  NestedSplit ns;
  const Split innermost = splitArchivePath(path);
  if (!innermost.valid) return ns;  // valid=false

  ns.innerPath = innermost.innerPath;

  // 最内から外へ 1 段ずつ剥がしていく。剥がした「アーカイブ内パス」は
  // 逆順に積まれるので、最後に外側から並ぶよう反転する。
  QString spec = innermost.archivePath;
  while (true) {
    const Split outer = splitArchivePath(spec);
    if (!outer.valid) break;
    // outer.innerPath は先頭 '/' 付き。エントリのキーは '/' なし形式に揃える。
    QString entry = outer.innerPath;
    while (entry.startsWith(QLatin1Char('/'))) entry.remove(0, 1);
    ns.innerArchives.prepend(entry);
    spec = outer.archivePath;
  }
  ns.rootPath = spec;
  ns.valid    = true;
  return ns;
}

QString displayPath(const QString& path) {
  // 区切りが無ければ通常のパス。splitNestedArchivePath は glob 照合をするので、
  // 大多数を占める通常パスは先に弾いておく。
  if (!path.contains(QLatin1Char('!'))) return path;

  const NestedSplit ns = splitNestedArchivePath(path);
  if (!ns.valid) return path;

  static const QString sep = QStringLiteral(" > ");
  QString out = ns.rootPath;
  for (const QString& inner : ns.innerArchives) {
    out += sep + inner;
  }
  QString tail = ns.innerPath;
  while (tail.startsWith(QLatin1Char('/'))) tail.remove(0, 1);
  if (!tail.isEmpty()) {
    out += sep + tail;
  }
  return out;
}

int archiveNestingLevel(const QString& path) {
  const NestedSplit ns = splitNestedArchivePath(path);
  return ns.valid ? ns.innerArchives.size() : 0;
}

QString parentInnerPath(const QString& innerPath) {
  if (innerPath.isEmpty() || innerPath == QStringLiteral("/")) {
    return QStringLiteral("/");
  }
  const int slash = innerPath.lastIndexOf(QLatin1Char('/'));
  if (slash <= 0) return QStringLiteral("/");
  return innerPath.left(slash);
}

QString innerBaseName(const QString& innerPath) {
  if (innerPath.isEmpty() || innerPath == QStringLiteral("/")) return {};
  const int slash = innerPath.lastIndexOf(QLatin1Char('/'));
  return innerPath.mid(slash + 1);
}

QString safeJoinExtractPath(const QString& outputDir, const QString& entryName) {
  if (entryName.isEmpty() || outputDir.isEmpty()) return {};

  // Windows のバックスラッシュも内部表現の '/' に揃えてから判定する。
  QString rel = QDir::fromNativeSeparators(entryName);

  // libarchive のディレクトリエントリは `emptydir/` のように末尾 '/' を
  // 付けてくる。これを正常エントリ扱いするため、検査前に末尾 '/' を
  // 1 文字落とす。連続 '/' (`foo//bar/`) は KeepEmptyParts で次の検査が
  // 拾うのでここでは 1 個だけ落とす。
  if (rel.size() > 1 && rel.endsWith(QLatin1Char('/'))) {
    rel.chop(1);
  }

  // 絶対パス (Unix `/...`、Windows `C:\...` / `\\server\...`) は拒否。
  // 末尾 '/' を落とした後でも判定対象は変わらない。
  if (QDir::isAbsolutePath(rel)) return {};

  // 分解して `..` / `.` セグメント、NUL 文字、連続スラッシュ (空セグメント) を弾く。
  // 通常エントリは `foo/bar/baz` のような清潔な形だが、悪意あるアーカイブは
  // `../etc/passwd` や `foo/../../../../etc/passwd` を含む。
  const QStringList parts = rel.split(QLatin1Char('/'), Qt::KeepEmptyParts);
  QStringList clean;
  clean.reserve(parts.size());
  for (const QString& part : parts) {
    if (part.isEmpty()) return {};                       // 連続/末端の '/' は不正
    if (part == QStringLiteral("."))  return {};
    if (part == QStringLiteral("..")) return {};
    if (part.contains(QChar(0)))      return {};         // NUL 注入を弾く
    clean.append(part);
  }
  if (clean.isEmpty()) return {};

  const QString outClean = QDir::cleanPath(outputDir);
  const QString joined   = outClean + QLatin1Char('/') + clean.join(QLatin1Char('/'));
  const QString resolved = QDir::cleanPath(joined);

  // cleanPath 後にも outputDir 配下に収まっていることを念のため確認する
  // (Windows の大文字小文字差や、ヘンなセパレータが混入した場合の防御線)。
  if (resolved != outClean
      && !resolved.startsWith(outClean + QLatin1Char('/'))) {
    return {};
  }
  return resolved;
}

} // namespace Farman::ArchivePath
