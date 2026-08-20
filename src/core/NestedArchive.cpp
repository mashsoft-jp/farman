#include "NestedArchive.h"

#include "settings/Settings.h"
#include "utils/ArchivePath.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <memory>

namespace Farman::NestedArchive {

namespace {

// 展開済み 1 件ぶん。大元のアーカイブの更新を検出するために、登録時点の
// mtime とサイズを覚えておく。
struct Entry {
  QString   localPath;
  QDateTime rootMTime;
  qint64    rootSize = -1;
};

QMutex& mutex() {
  static QMutex m;
  return m;
}

QHash<QString, Entry>& entries() {
  static QHash<QString, Entry> map;
  return map;
}

// セッション限りの一時ディレクトリ。アプリ終了時に丸ごと消える
// (= QTemporaryDir のデストラクタ)。置き場所は設定 → アーカイブの
// 「一時ディレクトリ」。関数内 static なので設定変更は次回起動から効く。
QString tempRoot() {
  static std::unique_ptr<QTemporaryDir> dir = [] {
    return std::make_unique<QTemporaryDir>(
      Settings::instance().effectiveArchiveTempDirectory()
        + QStringLiteral("/farman-nested-XXXXXX"));
  }();
  return (dir && dir->isValid()) ? dir->path() : QString();
}

// チェーンの一番外側 = 実 FS 上のアーカイブ。ここが更新されたら、内側の
// 展開済みファイルはすべて古い。
QFileInfo rootInfo(const QString& spec) {
  const ArchivePath::NestedSplit ns = ArchivePath::splitNestedArchivePath(spec);
  return QFileInfo(ns.valid ? ns.rootPath : spec);
}

// spec ごとに一意で、かつ元の名前が見て分かるファイル名。
// ハッシュだけだとログや一時ディレクトリを覗いたときに何の展開か分からない。
QString localFileNameFor(const QString& spec) {
  const QString hash = QString::fromLatin1(
    QCryptographicHash::hash(spec.toUtf8(), QCryptographicHash::Sha1)
      .toHex().left(16));
  QString name = QFileInfo(spec).fileName();
  // ファイル名に使えない文字は落とす (エントリ名由来なので何でも来うる)。
  name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")),
               QStringLiteral("_"));
  if (name.isEmpty()) name = QStringLiteral("archive");
  return hash + QLatin1Char('-') + name;
}

} // namespace

QString cachedLocalPath(const QString& spec) {
  QMutexLocker lock(&mutex());
  const auto it = entries().constFind(spec);
  if (it == entries().cend()) return QString();

  if (!QFileInfo::exists(it->localPath)) return QString();

  const QFileInfo root = rootInfo(spec);
  if (root.lastModified() != it->rootMTime || root.size() != it->rootSize) {
    return QString();  // 大元が更新された → 展開し直させる
  }
  return it->localPath;
}

QString reserveLocalPath(const QString& spec) {
  const QString root = tempRoot();
  if (root.isEmpty()) return QString();
  return root + QLatin1Char('/') + localFileNameFor(spec);
}

void registerLocalPath(const QString& spec, const QString& localPath) {
  const QFileInfo root = rootInfo(spec);
  QMutexLocker lock(&mutex());
  entries().insert(spec, Entry{localPath, root.lastModified(), root.size()});
}

} // namespace Farman::NestedArchive
