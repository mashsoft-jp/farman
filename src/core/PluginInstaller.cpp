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
#include <QRegularExpression>

#include <algorithm>

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

QString PluginInstaller::pluginBaseName(const QString& fileName) {
  QString base = QFileInfo(fileName).completeBaseName();
  // ブラウザが同名ダウンロードに付ける " (1)" を落とす。
  static const QRegularExpression dupSuffix(QStringLiteral("\\s*\\(\\d+\\)$"));
  base.remove(dupSuffix);
  // 配布物の "-v0.3.0-macos-arm64" / "-0.3.0" 以降を落とす。
  static const QRegularExpression versionTail(
    QStringLiteral("[-_]v?\\d+(\\.\\d+)+([-_.].*)?$"),
    QRegularExpression::CaseInsensitiveOption);
  base.remove(versionTail);
  return base.toLower();
}

QStringList PluginInstaller::samePluginFiles(const QString& root, Kind kind,
                                             const QString& fileName) {
  const QString sub = kindSubdir(kind);
  if (sub.isEmpty()) return {};
  const QString base = pluginBaseName(fileName);
  QStringList relPaths;
  const QStringList dirs = {root + QLatin1Char('/') + sub,
                            pendingDir(root) + QLatin1Char('/') + sub};
  for (const QString& dirPath : dirs) {
    const QStringList names = QDir(dirPath).entryList(QDir::Files, QDir::Name);
    for (const QString& name : names) {
      const QString relPath = sub + QLatin1Char('/') + name;
      if (pluginBaseName(name) == base && !relPaths.contains(relPath)) {
        relPaths.append(relPath);
      }
    }
  }
  return relPaths;
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

  // 同じプラグインの導入待ち (同名、または別の版) は、この導入で置き換える。
  const QString base = pluginBaseName(src.fileName());
  const QStringList pendingNames = QDir(destDir).entryList(QDir::Files, QDir::Name);
  for (const QString& name : pendingNames) {
    if (pluginBaseName(name) != base) continue;
    if (!QFile::remove(destDir + QLatin1Char('/') + name)) {
      return fail(tr("Cannot write to the plugins directory (%1).").arg(root));
    }
  }

  const QString dest = destDir + QLatin1Char('/') + src.fileName();
  if (!QFile::copy(src.absoluteFilePath(), dest)) {
    return fail(tr("Cannot write to the plugins directory (%1).").arg(root));
  }

  // 削除待ちを組み直す:
  //   - 同じファイル名の削除待ちは取り消す (後から行った導入が勝つ)
  //   - 以前の導入待ちに伴う削除 (replacedBy がこのプラグイン) は付け替える
  //   - 導入済みの別名ファイル (古い版) は、この導入に伴う削除として退避する。
  //     残すと同じ pluginId が 2 つになり、ファイル名順で先の古い版が読まれてしまう。
  QList<Removal> removals = readRemovals(root);
  for (int i = removals.size() - 1; i >= 0; --i) {
    const Removal& r = removals[i];
    const bool sameFile   = r.relPath == relPath;
    const bool staleLink  = !r.replacedBy.isEmpty()
                         && pluginBaseName(r.replacedBy) == base
                         && r.replacedBy.section(QLatin1Char('/'), 0, 0) == sub;
    if (sameFile || staleLink) {
      removals.removeAt(i);
    }
  }
  const QStringList installedNames =
    QDir(root + QLatin1Char('/') + sub).entryList(QDir::Files, QDir::Name);
  for (const QString& name : installedNames) {
    if (name == src.fileName() || pluginBaseName(name) != base) continue;
    const QString oldRelPath = sub + QLatin1Char('/') + name;
    bool alreadyStaged = false;
    for (Removal& r : removals) {
      if (r.relPath == oldRelPath) {
        r.replacedBy = relPath;  // ユーザー指示の削除待ちでも、結果は同じ (消える)
        alreadyStaged = true;
      }
    }
    if (!alreadyStaged) {
      removals.append({oldRelPath, relPath});
    }
  }
  if (!writeRemovals(root, removals)) {
    QFile::remove(dest);
    return fail(tr("Cannot write to the plugins directory (%1).").arg(root));
  }

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

  // このプラグインの導入待ち (同名の上書き、または別名の新しい版) があれば取り消す
  // (後から行った削除が勝つ)。取り消しは、それに伴う削除待ちも外す。
  const QString sub  = relPath.section(QLatin1Char('/'), 0, 0);
  const QString base = pluginBaseName(relPath);
  const QStringList pendingNames =
    QDir(pendingDir(root) + QLatin1Char('/') + sub).entryList(QDir::Files, QDir::Name);
  for (const QString& name : pendingNames) {
    if (pluginBaseName(name) == base) {
      cancelPendingInstall(root, sub + QLatin1Char('/') + name);
    }
  }

  QList<Removal> removals = readRemovals(root);
  bool found = false;
  for (Removal& r : removals) {
    if (r.relPath == relPath) {
      r.replacedBy.clear();
      found = true;
    }
  }
  if (!found) {
    removals.append({relPath, QString()});
  }
  if (!QDir().mkpath(pendingDir(root)) || !writeRemovals(root, removals)) {
    if (error) *error = tr("Cannot write to the plugins directory (%1).").arg(root);
    return false;
  }

  Logger::instance().info(
    QStringLiteral("Plugins: staged removal of %1 (applied on next start)").arg(relPath));
  return true;
}

bool PluginInstaller::cancelPendingInstall(const QString& root, const QString& relPath) {
  if (!isValidRelPath(relPath)) return false;
  const QString path = pendingDir(root) + QLatin1Char('/') + relPath;
  if (!QFileInfo::exists(path)) return false;
  if (!QFile::remove(path)) return false;
  // この導入に伴って退避された古い版の削除も取り消す (古い版を使い続ける)。
  QList<Removal> removals = readRemovals(root);
  const int before = removals.size();
  removals.removeIf([&relPath](const Removal& r) { return r.replacedBy == relPath; });
  if (removals.size() != before) {
    writeRemovals(root, removals);
  }
  removeEmptyPendingDirs(root);
  Logger::instance().info(
    QStringLiteral("Plugins: cancelled staged install of %1").arg(relPath));
  return true;
}

bool PluginInstaller::cancelPendingRemoval(const QString& root, const QString& relPath) {
  QList<Removal> removals = readRemovals(root);
  const int before = removals.size();
  removals.removeIf([&relPath](const Removal& r) { return r.relPath == relPath; });
  if (removals.size() == before) return false;
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
  const QList<Removal> removals = readRemovals(root);
  for (const Removal& r : removals) {
    state.removals.append(r.relPath);
    if (!r.replacedBy.isEmpty()) {
      state.replacedBy.insert(r.relPath, r.replacedBy);
    }
  }
  return state;
}

void PluginInstaller::applyPending(const QString& root) {
  // (1) 削除。失敗したものだけ removals.json に残して次回再試行する。
  const QList<Removal> removals = readRemovals(root);
  if (!removals.isEmpty()) {
    QList<Removal> remaining;
    for (const Removal& removal : removals) {
      const QString& relPath = removal.relPath;
      const QString path = root + QLatin1Char('/') + relPath;
      if (!QFileInfo::exists(path)) {
        continue;  // 既に無い (手で消された等) なら完了扱い
      }
      // 更新に伴う削除は、置き換えるファイルが退避に残っているときだけ行う
      // (導入待ちだけ手で消された場合に、古い版まで失わないように)。
      if (!removal.replacedBy.isEmpty()
          && !QFileInfo::exists(pendingDir(root) + QLatin1Char('/') + removal.replacedBy)) {
        Logger::instance().warn(
          QStringLiteral("Plugins: kept %1 (its replacement %2 is missing)")
            .arg(relPath, removal.replacedBy));
        continue;
      }
      if (QFile::remove(path)) {
        Logger::instance().info(
          removal.replacedBy.isEmpty()
            ? QStringLiteral("Plugins: uninstalled %1").arg(relPath)
            : QStringLiteral("Plugins: removed %1 (replaced by %2)")
                .arg(relPath, removal.replacedBy));
      } else {
        remaining.append(removal);
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

QList<PluginInstaller::Removal> PluginInstaller::readRemovals(const QString& root) {
  QFile file(removalsFilePath(root));
  if (!file.open(QIODevice::ReadOnly)) return {};
  const QJsonArray array = QJsonDocument::fromJson(file.readAll())
                             .object()
                             .value(QStringLiteral("removals"))
                             .toArray();
  // 要素は "viewers/Foo.dylib" (アンインストール) か、
  // {"path": ..., "replacedBy": ...} (更新に伴う古い版の削除)。
  QList<Removal> removals;
  for (const QJsonValue& v : array) {
    Removal removal;
    if (v.isObject()) {
      const QJsonObject obj = v.toObject();
      removal.relPath    = obj.value(QStringLiteral("path")).toString();
      removal.replacedBy = obj.value(QStringLiteral("replacedBy")).toString();
      if (!removal.replacedBy.isEmpty() && !isValidRelPath(removal.replacedBy)) {
        continue;
      }
    } else {
      removal.relPath = v.toString();
    }
    if (!isValidRelPath(removal.relPath)) continue;
    const bool duplicate = std::any_of(
      removals.cbegin(), removals.cend(),
      [&removal](const Removal& r) { return r.relPath == removal.relPath; });
    if (!duplicate) {
      removals.append(removal);
    }
  }
  return removals;
}

bool PluginInstaller::writeRemovals(const QString& root, const QList<Removal>& removals) {
  const QString path = removalsFilePath(root);
  if (removals.isEmpty()) {
    return !QFileInfo::exists(path) || QFile::remove(path);
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  QJsonArray array;
  for (const Removal& removal : removals) {
    if (removal.replacedBy.isEmpty()) {
      array.append(removal.relPath);
    } else {
      QJsonObject entry;
      entry.insert(QStringLiteral("path"), removal.relPath);
      entry.insert(QStringLiteral("replacedBy"), removal.replacedBy);
      array.append(entry);
    }
  }
  QJsonObject obj;
  obj.insert(QStringLiteral("removals"), array);
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
