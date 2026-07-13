#include "ArchiveDispatcher.h"
#include "Logger.h"
#include "settings/Settings.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QPluginLoader>
#include <limits>

namespace Farman {

ArchiveDispatcher& ArchiveDispatcher::instance() {
  static ArchiveDispatcher inst;
  return inst;
}

ArchiveDispatcher::ArchiveDispatcher(QObject* parent) : QObject(parent) {}

QStringList ArchiveDispatcher::bundledArchiveDirectories() const {
  QStringList dirs;
  const QString appDirPath = QCoreApplication::applicationDirPath();
  QDir appDir(appDirPath);

#ifdef Q_OS_MAC
  QDir macBundleDir(appDirPath);
  if (macBundleDir.dirName() == QLatin1String("MacOS")
      && macBundleDir.cdUp()
      && macBundleDir.cd(QStringLiteral("PlugIns"))) {
    dirs.append(macBundleDir.filePath(QStringLiteral("archives")));
  }
#endif

  dirs.append(appDir.filePath(QStringLiteral("plugins/archives")));
  QDir parentDir(appDirPath);
  if (parentDir.cdUp()) {
    dirs.append(parentDir.filePath(QStringLiteral("plugins/archives")));
  }
  dirs.removeDuplicates();
  return dirs;
}

void ArchiveDispatcher::registerBundledPlugins() {
  const QStringList candidates = bundledArchiveDirectories();
  for (const QString& path : candidates) {
    const QDir dir(path);
    if (dir.exists()) {
      loadPluginsFromDirectory(dir, ArchivePluginRecord::Origin::Bundled);
      return;
    }
  }
  Logger::instance().info(
    QStringLiteral("ArchivePlugins: bundled archive plugin directory not found (%1)")
      .arg(candidates.join(QStringLiteral(", "))));
}

void ArchiveDispatcher::loadPlugins(const QDir& pluginDir) {
  loadPluginsFromDirectory(pluginDir, ArchivePluginRecord::Origin::External);
}

void ArchiveDispatcher::loadPluginsFromDirectory(
    const QDir& pluginDir, ArchivePluginRecord::Origin origin) {
  if (!pluginDir.exists()) {
    Logger::instance().info(
      QStringLiteral("ArchivePlugins: directory not found, skipping (%1)")
        .arg(pluginDir.absolutePath()));
    return;
  }

  QStringList filters;
  filters << "*.so" << "*.dylib" << "*.dll";
  const QFileInfoList files = pluginDir.entryInfoList(filters, QDir::Files);

  for (const QFileInfo& fileInfo : files) {
    ArchivePluginRecord rec;
    rec.origin   = origin;
    rec.filePath = fileInfo.absoluteFilePath();

    auto loader = std::make_shared<QPluginLoader>(rec.filePath);

    // Qt プラグインのメタデータ (IID) を持たないファイルは Qt プラグインでは
    // ない (依存ネイティブ DLL 等)。一覧に紛れないよう静かにスキップする。
    if (loader->metaData().value(QStringLiteral("IID")).toString().isEmpty()) {
      Logger::instance().info(
        QStringLiteral("ArchivePlugins: skipping non-plugin library %1")
          .arg(fileInfo.fileName()));
      continue;
    }

    QObject* obj = loader->instance();
    if (!obj) {
      rec.loaded      = false;
      rec.errorReason = loader->errorString();
      m_records.append(rec);
      loader->unload();
      Logger::instance().warn(
        QStringLiteral("ArchivePlugins: failed to load %1 (%2)")
          .arg(fileInfo.fileName(), rec.errorReason));
      continue;
    }

    IArchivePlugin* plugin = qobject_cast<IArchivePlugin*>(obj);
    if (!plugin) {
      rec.loaded      = false;
      rec.errorReason = tr("Not an IArchivePlugin (wrong IID?)");
      m_records.append(rec);
      loader->unload();
      Logger::instance().warn(
        QStringLiteral("ArchivePlugins: %1 is not IArchivePlugin (wrong IID?)")
          .arg(fileInfo.fileName()));
      continue;
    }

    rec.pluginId            = plugin->pluginId();
    rec.pluginName          = plugin->pluginName();
    rec.version             = plugin->version();
    rec.author              = plugin->author();
    rec.authorUrl           = plugin->authorUrl();
    rec.supportedExtensions = plugin->supportedExtensions();
    rec.priority            = plugin->priority();

    // 外部プラグインのみ検証を強制する (ViewerDispatcher と同基準)。
    // バンドル公式プラグインは priority 10000+ の予約域を使うため、
    // 0〜9999 制限や author 必須の対象外とする。
    if (origin == ArchivePluginRecord::Origin::External) {
      // 外部プラグインの優先度は 0〜9999 のみ許可。
      if (rec.priority < 0 || rec.priority > 9999) {
        rec.loaded      = false;
        rec.errorReason =
          tr("Invalid priority %1 (external plugins must use 0-9999)")
            .arg(rec.priority);
        m_records.append(rec);
        loader->unload();
        Logger::instance().warn(
          QStringLiteral("ArchivePlugins: rejected '%1' (%2): priority %3 out of range")
            .arg(rec.pluginId, fileInfo.fileName())
            .arg(rec.priority));
        continue;
      }
      // 外部プラグインは制作者情報 (author) を必須とする。
      if (rec.author.trimmed().isEmpty()) {
        rec.loaded      = false;
        rec.errorReason =
          tr("Missing author information (external plugins must declare author())");
        m_records.append(rec);
        loader->unload();
        Logger::instance().warn(
          QStringLiteral("ArchivePlugins: rejected '%1' (%2): author() is empty")
            .arg(rec.pluginId, fileInfo.fileName()));
        continue;
      }
    }

    // ユーザーが設定で無効化したプラグインは登録しない (ViewerDispatcher と同じ)。
    if (Settings::instance().isArchivePluginDisabled(rec.pluginId)) {
      rec.loaded         = false;
      rec.disabledByUser = true;
      rec.errorReason    = tr("Disabled by user");
      m_records.append(rec);
      loader->unload();
      Logger::instance().info(
        QStringLiteral("ArchivePlugins: disabled '%1' (%2)")
          .arg(rec.pluginId, fileInfo.fileName()));
      continue;
    }

    // initialize() を呼び、失敗したら登録を取り消す。
    if (!plugin->initialize(m_context)) {
      rec.loaded      = false;
      rec.errorReason = tr("initialize() failed");
      m_records.append(rec);
      loader->unload();
      Logger::instance().warn(
        QStringLiteral("ArchivePlugins: '%1' initialize() failed")
          .arg(rec.pluginId));
      continue;
    }

    // QPluginLoader が QObject のライフタイムを管理するので、shared_ptr 側は
    // delete しない deleter を使う。
    m_plugins.append(
      std::shared_ptr<IArchivePlugin>(plugin, [](IArchivePlugin*) {}));
    m_pluginLoaders.append(loader);
    rec.loaded = true;
    m_records.append(rec);
    Logger::instance().info(
      QStringLiteral("ArchivePlugins: loaded '%1' v%2 (%3) exts=[%4] prio=%5")
        .arg(rec.pluginId,
             rec.version.isEmpty() ? QStringLiteral("?") : rec.version,
             fileInfo.fileName(),
             rec.supportedExtensions.join(QLatin1Char(',')))
        .arg(rec.priority));
  }
}

void ArchiveDispatcher::shutdownPlugins() {
  for (const auto& plugin : m_plugins) {
    if (plugin) plugin->shutdown();
  }
  m_plugins.clear();
  for (const auto& loader : m_pluginLoaders) {
    if (loader) loader->unload();
  }
  m_pluginLoaders.clear();
}

IArchivePlugin* ArchiveDispatcher::pluginForPath(const QString& archivePath) const {
  IArchivePlugin* best = nullptr;
  int bestPrio = std::numeric_limits<int>::max();
  for (const auto& plugin : m_plugins) {
    if (!plugin) continue;
    if (!plugin->canHandle(archivePath)) continue;
    if (plugin->priority() < bestPrio) {
      best     = plugin.get();
      bestPrio = plugin->priority();
    }
  }
  return best;
}

QStringList ArchiveDispatcher::registeredDotExtensions() const {
  QStringList out;
  for (const auto& plugin : m_plugins) {
    if (!plugin) continue;
    for (const QString& ext : plugin->supportedExtensions()) {
      if (ext.isEmpty()) continue;
      const QString dotted =
        ext.startsWith(QLatin1Char('.')) ? ext : (QLatin1Char('.') + ext);
      if (!out.contains(dotted, Qt::CaseInsensitive)) out.append(dotted);
    }
  }
  return out;
}

} // namespace Farman
