#include "PluginInstaller.h"

#include "IArchivePlugin.h"
#include "Logger.h"
#include "settings/Settings.h"
#include "utils/PluginCompat.h"
#include "viewer/IViewerPlugin.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>

namespace Farman {

namespace {

const QLatin1String kViewerIidPrefix("com.farman.IViewerPlugin/");
const QLatin1String kArchiveIidPrefix("com.farman.IArchivePlugin/");

// "com.farman.IViewerPlugin/5.0" → "5.0"
QString iidVersion(const QString& iid) {
  return iid.section(QLatin1Char('/'), 1);
}

// removals.json / pending の相対パスとして妥当か ("viewers/x" / "archives/x" のみ)。
// 手で書き換えられた removals.json でディレクトリ外を消させないための検査。
bool isValidRelPath(const QString& relPath) {
  const QStringList parts = relPath.split(QLatin1Char('/'));
  if (parts.size() != 2) return false;
  if (parts[0] != QLatin1String("viewers") && parts[0] != QLatin1String("archives")) {
    return false;
  }
  const QString& name = parts[1];
  return !name.isEmpty() && name != QLatin1String(".") && name != QLatin1String("..")
      && !name.contains(QLatin1Char('\\'));
}

} // namespace

QString PluginInstaller::pluginsRoot() {
  const QString dir = Settings::instance().pluginsDirectory();
  return dir.isEmpty() ? Settings::defaultPluginsDirectory() : dir;
}

QString PluginInstaller::hostVersion() {
#ifdef FARMAN_VERSION
  return QStringLiteral(QT_STRINGIFY(FARMAN_VERSION));
#else
  return QString();
#endif
}

QString PluginInstaller::kindSubdir(Kind kind) {
  switch (kind) {
    case Kind::Viewer:  return QStringLiteral("viewers");
    case Kind::Archive: return QStringLiteral("archives");
    case Kind::Unknown: break;
  }
  return QString();
}

QString PluginInstaller::nativeLibrarySuffix() {
#if defined(Q_OS_MACOS)
  return QStringLiteral("dylib");
#elif defined(Q_OS_WIN)
  return QStringLiteral("dll");
#else
  return QStringLiteral("so");
#endif
}

PluginInstaller::Inspection PluginInstaller::inspect(const QString& filePath,
                                                     const QString& hostVersion) {
  Inspection result;
  const QFileInfo fi(filePath);
  if (!fi.isFile()) {
    result.error = tr("Not a file.");
    return result;
  }

  const QString suffix = fi.suffix().toLower();
  if (suffix != nativeLibrarySuffix()) {
    const bool otherOs = suffix == QLatin1String("dylib")
                      || suffix == QLatin1String("dll")
                      || suffix == QLatin1String("so");
    result.error = otherOs
      ? tr("This plugin is not for this OS (expected a .%1 file).")
          .arg(nativeLibrarySuffix())
      : tr("Not a plugin file (expected a .%1 file).").arg(nativeLibrarySuffix());
    return result;
  }

  // metaData() はライブラリをロードせず、バイナリ内の Qt メタデータを読むだけ。
  const QPluginLoader loader(fi.absoluteFilePath());
  const QJsonObject meta = loader.metaData();
  result.iid = meta.value(QStringLiteral("IID")).toString();

  QString hostIid;
  if (result.iid.startsWith(kViewerIidPrefix)) {
    result.kind = Kind::Viewer;
    hostIid = QStringLiteral(FarmanIViewerPlugin_iid);
  } else if (result.iid.startsWith(kArchiveIidPrefix)) {
    result.kind = Kind::Archive;
    hostIid = QStringLiteral(FarmanIArchivePlugin_iid);
  } else {
    result.error = tr("Not a farman plugin.");
    return result;
  }

  if (result.iid != hostIid) {
    result.error =
      tr("This plugin is not compatible with this farman "
         "(plugin interface %1, farman expects %2).")
        .arg(iidVersion(result.iid), iidVersion(hostIid));
    return result;
  }

  result.minHostVersion = meta.value(QStringLiteral("MetaData"))
                              .toObject()
                              .value(QStringLiteral("MinHostVersion"))
                              .toString();
  if (!hostSatisfiesMinVersion(hostVersion, result.minHostVersion)) {
    result.error = tr("This plugin requires farman %1 or later (current %2).")
                     .arg(result.minHostVersion, hostVersion);
    return result;
  }

  result.ok = true;
  return result;
}

bool PluginInstaller::isInstalledOrPending(const QString& root, Kind kind,
                                           const QString& fileName) {
  const QString sub = kindSubdir(kind);
  if (sub.isEmpty()) return false;
  return QFileInfo::exists(root + QLatin1Char('/') + sub + QLatin1Char('/') + fileName)
      || QFileInfo::exists(pendingDir(root) + QLatin1Char('/') + sub
                           + QLatin1Char('/') + fileName);
}

bool PluginInstaller::stageInstall(const QString& root, const QString& filePath,
                                   Kind kind, QString* error) {
  const auto fail = [error](const QString& reason) {
    if (error) *error = reason;
    return false;
  };

  const QString sub = kindSubdir(kind);
  if (sub.isEmpty()) return fail(tr("Unknown plugin type."));

  const QFileInfo src(filePath);
  const QString relPath = sub + QLatin1Char('/') + src.fileName();

  // 導入済みの場所にあるファイルそのものを落とされた場合は何もしない
  // (自分自身へのコピーになる)。
  const QFileInfo installed(root + QLatin1Char('/') + relPath);
  if (installed.exists()
      && installed.canonicalFilePath() == src.canonicalFilePath()) {
    return fail(tr("This file is already in the plugins directory."));
  }

  const QString destDir = pendingDir(root) + QLatin1Char('/') + sub;
  if (!QDir().mkpath(destDir)) {
    return fail(tr("Cannot write to the plugins directory (%1).").arg(root));
  }

  const QString dest = destDir + QLatin1Char('/') + src.fileName();
  if (QFileInfo::exists(dest) && !QFile::remove(dest)) {
    return fail(tr("Cannot write to the plugins directory (%1).").arg(root));
  }
  if (!QFile::copy(src.absoluteFilePath(), dest)) {
    return fail(tr("Cannot write to the plugins directory (%1).").arg(root));
  }

  // 同じファイルの削除待ちがあれば取り消す (後から行った導入が勝つ)。
  cancelPendingRemoval(root, relPath);

  Logger::instance().info(
    QStringLiteral("Plugins: staged install of %1 (applied on next start)").arg(relPath));
  return true;
}

bool PluginInstaller::stageRemoval(const QString& root,
                                   const QString& installedFilePath,
                                   QString* error) {
  const QString relPath = managedRelativePath(root, installedFilePath);
  if (relPath.isEmpty()) {
    if (error) *error = tr("Only plugins in the plugins directory can be uninstalled.");
    return false;
  }

  QStringList removals = readRemovals(root);
  if (!removals.contains(relPath)) {
    removals.append(relPath);
  }
  if (!QDir().mkpath(pendingDir(root)) || !writeRemovals(root, removals)) {
    if (error) *error = tr("Cannot write to the plugins directory (%1).").arg(root);
    return false;
  }

  // 同じファイルの導入待ち (更新) があれば取り消す (後から行った削除が勝つ)。
  cancelPendingInstall(root, relPath);

  Logger::instance().info(
    QStringLiteral("Plugins: staged removal of %1 (applied on next start)").arg(relPath));
  return true;
}

bool PluginInstaller::cancelPendingInstall(const QString& root, const QString& relPath) {
  if (!isValidRelPath(relPath)) return false;
  const QString path = pendingDir(root) + QLatin1Char('/') + relPath;
  if (!QFileInfo::exists(path)) return false;
  if (!QFile::remove(path)) return false;
  removeEmptyPendingDirs(root);
  Logger::instance().info(
    QStringLiteral("Plugins: cancelled staged install of %1").arg(relPath));
  return true;
}

bool PluginInstaller::cancelPendingRemoval(const QString& root, const QString& relPath) {
  QStringList removals = readRemovals(root);
  if (removals.removeAll(relPath) == 0) return false;
  if (!writeRemovals(root, removals)) return false;
  removeEmptyPendingDirs(root);
  Logger::instance().info(
    QStringLiteral("Plugins: cancelled staged removal of %1").arg(relPath));
  return true;
}

PluginInstaller::PendingState PluginInstaller::pendingState(const QString& root) {
  PendingState state;
  for (const Kind kind : {Kind::Viewer, Kind::Archive}) {
    const QString sub = kindSubdir(kind);
    const QDir dir(pendingDir(root) + QLatin1Char('/') + sub);
    const QStringList names = dir.entryList(QDir::Files, QDir::Name);
    for (const QString& name : names) {
      state.installs.append(sub + QLatin1Char('/') + name);
    }
  }
  state.removals = readRemovals(root);
  return state;
}

void PluginInstaller::applyPending(const QString& root) {
  // (1) 削除。失敗したものだけ removals.json に残して次回再試行する。
  const QStringList removals = readRemovals(root);
  if (!removals.isEmpty()) {
    QStringList remaining;
    for (const QString& relPath : removals) {
      const QString path = root + QLatin1Char('/') + relPath;
      if (!QFileInfo::exists(path)) {
        continue;  // 既に無い (手で消された等) なら完了扱い
      }
      if (QFile::remove(path)) {
        Logger::instance().info(
          QStringLiteral("Plugins: uninstalled %1").arg(relPath));
      } else {
        remaining.append(relPath);
        Logger::instance().warn(
          QStringLiteral("Plugins: failed to uninstall %1 (will retry on next start)")
            .arg(relPath));
      }
    }
    writeRemovals(root, remaining);
  }

  // (2) 導入 / 更新。pending/<種別>/ の各ファイルを本来の場所へ移す。
  for (const Kind kind : {Kind::Viewer, Kind::Archive}) {
    const QString sub = kindSubdir(kind);
    const QDir srcDir(pendingDir(root) + QLatin1Char('/') + sub);
    if (!srcDir.exists()) continue;

    const QString destDir = root + QLatin1Char('/') + sub;
    QDir().mkpath(destDir);

    const QStringList names = srcDir.entryList(QDir::Files, QDir::Name);
    for (const QString& name : names) {
      const QString relPath = sub + QLatin1Char('/') + name;
      const QString src  = srcDir.filePath(name);
      const QString dest = destDir + QLatin1Char('/') + name;
      const bool updating = QFileInfo::exists(dest);
      if (updating && !QFile::remove(dest)) {
        Logger::instance().warn(
          QStringLiteral("Plugins: failed to replace %1 (will retry on next start)")
            .arg(relPath));
        continue;
      }
      if (QFile::rename(src, dest)) {
        Logger::instance().info(
          QStringLiteral("Plugins: %1 %2")
            .arg(updating ? QStringLiteral("updated") : QStringLiteral("installed"),
                 relPath));
      } else {
        Logger::instance().warn(
          QStringLiteral("Plugins: failed to install %1 (will retry on next start)")
            .arg(relPath));
      }
    }
  }

  removeEmptyPendingDirs(root);
}

void PluginInstaller::removeEmptyPendingDirs(const QString& root) {
  // QDir::rmdir は空のディレクトリしか消さないので、退避が残っていれば何も起きない。
  QDir dir(pendingDir(root));
  if (!dir.exists()) return;
  for (const Kind kind : {Kind::Viewer, Kind::Archive}) {
    dir.rmdir(kindSubdir(kind));
  }
  QDir(root).rmdir(QStringLiteral("pending"));
}

QString PluginInstaller::pendingDir(const QString& root) {
  return root + QStringLiteral("/pending");
}

QString PluginInstaller::removalsFilePath(const QString& root) {
  return pendingDir(root) + QStringLiteral("/removals.json");
}

QStringList PluginInstaller::readRemovals(const QString& root) {
  QFile file(removalsFilePath(root));
  if (!file.open(QIODevice::ReadOnly)) return {};
  const QJsonArray array = QJsonDocument::fromJson(file.readAll())
                             .object()
                             .value(QStringLiteral("removals"))
                             .toArray();
  QStringList relPaths;
  for (const QJsonValue& v : array) {
    const QString relPath = v.toString();
    if (isValidRelPath(relPath) && !relPaths.contains(relPath)) {
      relPaths.append(relPath);
    }
  }
  return relPaths;
}

bool PluginInstaller::writeRemovals(const QString& root, const QStringList& relPaths) {
  const QString path = removalsFilePath(root);
  if (relPaths.isEmpty()) {
    return !QFileInfo::exists(path) || QFile::remove(path);
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  QJsonObject obj;
  obj.insert(QStringLiteral("removals"), QJsonArray::fromStringList(relPaths));
  const QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Indented);
  return file.write(json) == json.size();
}

QString PluginInstaller::managedRelativePath(const QString& root,
                                             const QString& installedFilePath) {
  if (installedFilePath.isEmpty()) return QString();
  const QFileInfo fi(installedFilePath);
  const QString parent = QFileInfo(fi.absolutePath()).canonicalFilePath();
  if (parent.isEmpty()) return QString();
  for (const Kind kind : {Kind::Viewer, Kind::Archive}) {
    const QString sub = kindSubdir(kind);
    const QString dir = QFileInfo(root + QLatin1Char('/') + sub).canonicalFilePath();
    if (!dir.isEmpty() && dir == parent) {
      return sub + QLatin1Char('/') + fi.fileName();
    }
  }
  return QString();
}

} // namespace Farman
