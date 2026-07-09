#include "ArchivePath.h"
#include <QDir>
#include <QStringList>

namespace Farman::ArchivePath {

namespace {

// アーカイブとして扱う拡張子のリスト (大文字小文字無視)。
// 順序は長いもの優先 (.tar.gz が .tar より先にマッチしてほしい)。
const QStringList& archiveExtensions() {
  static const QStringList exts = {
    QStringLiteral(".tar.gz"), QStringLiteral(".tar.bz2"), QStringLiteral(".tar.xz"),
    QStringLiteral(".tgz"),    QStringLiteral(".tbz2"),    QStringLiteral(".txz"),
    QStringLiteral(".tar"),    QStringLiteral(".zip"),
  };
  return exts;
}

// プラグインが実行時に登録した拡張子 (先頭ドット付き)。registerArchiveExtension()
// で追加される。組み込みリストと違い可変だが、登録は起動時 (プラグインロード直後)
// に一度だけ行い、以後は読み取り専用として扱う想定。
QStringList& pluginExtensions() {
  static QStringList exts;
  return exts;
}

} // namespace

void registerArchiveExtension(const QString& dotExt) {
  if (dotExt.isEmpty()) return;
  const QString e =
    dotExt.startsWith(QLatin1Char('.')) ? dotExt : (QLatin1Char('.') + dotExt);
  if (!pluginExtensions().contains(e, Qt::CaseInsensitive)) {
    pluginExtensions().append(e);
  }
}

bool isArchiveExtension(const QString& path) {
  for (const QString& e : archiveExtensions()) {
    if (path.endsWith(e, Qt::CaseInsensitive)) return true;
  }
  for (const QString& e : pluginExtensions()) {
    if (path.endsWith(e, Qt::CaseInsensitive)) return true;
  }
  return false;
}

QString archiveBaseName(const QString& fileName) {
  // 組み込み + プラグイン登録の全拡張子から、末尾に一致する最長のものを剥がす
  // (".tar.gz" を ".tar" より優先させるため、長さ降順で最初に一致したものを採用)。
  QString best;
  auto consider = [&](const QString& e) {
    if (fileName.endsWith(e, Qt::CaseInsensitive) && e.size() > best.size()) {
      best = e;
    }
  };
  for (const QString& e : archiveExtensions()) consider(e);
  for (const QString& e : pluginExtensions()) consider(e);

  if (best.isEmpty() || best.size() >= fileName.size()) return fileName;
  QString baseName = fileName;
  baseName.chop(best.size());
  return baseName;
}

Split splitArchivePath(const QString& path) {
  Split s;
  const int sep = path.indexOf(QLatin1Char('!'));
  if (sep < 0) {
    s.archivePath = path;
    return s;  // valid=false
  }
  const QString archive = path.left(sep);
  if (!isArchiveExtension(archive)) {
    s.archivePath = path;
    return s;  // valid=false (`!` を含むが拡張子が違うので通常パス扱い)
  }
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
