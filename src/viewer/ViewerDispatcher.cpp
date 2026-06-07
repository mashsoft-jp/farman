#include "ViewerDispatcher.h"
#include "core/Logger.h"
#include "settings/Settings.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QPalette>
#include <QPluginLoader>

namespace Farman {

ViewerDispatcher& ViewerDispatcher::instance() {
  static ViewerDispatcher instance;
  return instance;
}

ViewerDispatcher::ViewerDispatcher(QObject* parent) : QObject(parent) {
}

PluginAppearance ViewerDispatcher::currentAppearance() {
  PluginAppearance appearance;
  const Settings& settings = Settings::instance();
  appearance.theme = settings.effectiveTheme() == ThemeMode::Dark
                       ? PluginAppearance::Theme::Dark
                       : PluginAppearance::Theme::Light;

  if (qApp) {
    const QPalette palette = qApp->palette();
    appearance.uiFont = qApp->font();
    appearance.windowBackground = palette.color(QPalette::Window);
    appearance.panelBackground = palette.color(QPalette::Base);
    appearance.text = palette.color(QPalette::Text);
    appearance.mutedText = palette.color(QPalette::Disabled, QPalette::Text);
    appearance.accent = palette.color(QPalette::Highlight);
    appearance.selectionBackground = palette.color(QPalette::Highlight);
    appearance.selectionText = palette.color(QPalette::HighlightedText);
  }

  return appearance;
}

void ViewerDispatcher::notifyAppearanceChanged(
  const PluginAppearance& appearance) {
  m_context.appearance = appearance;
  for (const auto& plugin : m_plugins) {
    if (plugin) {
      plugin->appearanceChanged(appearance);
    }
  }
}

void ViewerDispatcher::registerBundledPlugins() {
  const QStringList candidates = bundledPluginDirectories();
  for (const QString& path : candidates) {
    const QDir dir(path);
    if (dir.exists()) {
      loadPluginsFromDirectory(dir, PluginRecord::Origin::Bundled);
      return;
    }
  }

  Logger::instance().warn(
    QStringLiteral("Plugins: bundled viewer plugin directory not found (%1)")
      .arg(candidates.join(QStringLiteral(", "))));
}

void ViewerDispatcher::loadPlugins(const QDir& pluginDir) {
  loadPluginsFromDirectory(pluginDir, PluginRecord::Origin::External);
}

QStringList ViewerDispatcher::bundledPluginDirectories() const {
  QStringList dirs;
  const QString appDirPath = QCoreApplication::applicationDirPath();
  QDir appDir(appDirPath);

#ifdef Q_OS_MAC
  QDir macBundleDir(appDirPath);
  if (macBundleDir.dirName() == QLatin1String("MacOS")
      && macBundleDir.cdUp()
      && macBundleDir.cd(QStringLiteral("PlugIns"))) {
    dirs.append(macBundleDir.filePath(QStringLiteral("viewers")));
  }
#endif

  dirs.append(appDir.filePath(QStringLiteral("plugins/viewers")));
  QDir parentDir(appDirPath);
  if (parentDir.cdUp()) {
    dirs.append(parentDir.filePath(QStringLiteral("plugins/viewers")));
  }
  dirs.removeDuplicates();
  return dirs;
}

void ViewerDispatcher::loadPluginsFromDirectory(const QDir& pluginDir,
                                                PluginRecord::Origin origin) {
  if (!pluginDir.exists()) {
    Logger::instance().info(
      QStringLiteral("Plugins: directory not found, skipping (%1)")
        .arg(pluginDir.absolutePath()));
    return;
  }

  QStringList filters;
  filters << "*.so" << "*.dylib" << "*.dll";

  QFileInfoList plugins = pluginDir.entryInfoList(filters, QDir::Files);
  const int recordsBefore = m_records.size();
  for (const QFileInfo& fileInfo : plugins) {
    PluginRecord rec;
    rec.origin   = origin;
    rec.filePath = fileInfo.absoluteFilePath();

    auto loader = std::make_shared<QPluginLoader>(rec.filePath);
    QObject* plugin = loader->instance();
    if (!plugin) {
      rec.loaded      = false;
      rec.errorReason = loader->errorString();
      m_records.append(rec);
      loader->unload();
      Logger::instance().warn(
        QStringLiteral("Plugins: failed to load %1 (%2)")
          .arg(fileInfo.fileName(), rec.errorReason));
      continue;
    }
    IViewerPlugin* viewerPlugin = qobject_cast<IViewerPlugin*>(plugin);
    if (!viewerPlugin) {
      rec.loaded      = false;
      rec.errorReason = tr("Not an IViewerPlugin (wrong IID?)");
      m_records.append(rec);
      loader->unload();
      Logger::instance().warn(
        QStringLiteral("Plugins: %1 is not IViewerPlugin (wrong IID?)")
          .arg(fileInfo.fileName()));
      continue;
    }
    rec.pluginId = viewerPlugin->pluginId();
    rec.pluginName = viewerPlugin->pluginName();
    rec.supportedExtensions = viewerPlugin->supportedExtensions();
    if (origin == PluginRecord::Origin::External
        && Settings::instance().isViewerPluginDisabled(rec.pluginId)) {
      rec.loaded = false;
      rec.disabledByUser = true;
      rec.errorReason = tr("Disabled by user");
      m_records.append(rec);
      loader->unload();
      Logger::instance().info(
        QStringLiteral("Plugins: disabled '%1' (%2)")
          .arg(rec.pluginId, fileInfo.fileName()));
      continue;
    }
    // QPluginLoader が QObject (= IViewerPlugin の実体) のライフタイムを
    // 管理するので、shared_ptr 側は delete しない deleter を使う。
    // registerPlugin が成功・失敗ともに m_records に最終結果を追記する。
    registerPlugin(std::shared_ptr<IViewerPlugin>(viewerPlugin, [](IViewerPlugin*){}),
                   rec.filePath,
                   origin);
    if (!m_records.isEmpty() && m_records.last().loaded) {
      // 登録成功した外部プラグインだけ loader を保持し、プラグイン実体を
      // アプリ終了まで生かす。
      m_pluginLoaders.append(loader);
    } else {
      loader->unload();
    }
  }
  // 今回ロード分のサマリログ
  int loadedCount = 0;
  int failedCount = 0;
  for (int i = recordsBefore; i < m_records.size(); ++i) {
    if (m_records[i].loaded) ++loadedCount;
    else                      ++failedCount;
  }
  Logger::instance().info(
    QStringLiteral("Plugins: %1 loaded, %2 failed from %3 (%4)")
      .arg(loadedCount)
      .arg(failedCount)
      .arg(pluginDir.absolutePath(),
           origin == PluginRecord::Origin::Bundled
             ? QStringLiteral("bundled")
             : QStringLiteral("external")));
}

IViewerPlugin* ViewerDispatcher::resolvePlugin(const QString& filePath) const {
  if (filePath.isEmpty()) {
    return nullptr;
  }

  QFileInfo fileInfo(filePath);
  if (!fileInfo.exists() || !fileInfo.isFile()) {
    return nullptr;
  }

  QString extension = fileInfo.suffix().toLower();

  const QString preferredPluginId =
    Settings::instance().viewerAssociationForExtension(extension);
  if (!preferredPluginId.isEmpty()) {
    for (const auto& plugin : m_plugins) {
      if (plugin->pluginId() == preferredPluginId) {
        return plugin.get();
      }
    }
    Logger::instance().warn(
      QStringLiteral("Viewer association for .%1 points to missing plugin '%2'")
        .arg(extension, preferredPluginId));
  }

  // Find matching plugin with highest priority
  IViewerPlugin* bestMatch = nullptr;
  int highestPriority = -1;

  for (const auto& plugin : m_plugins) {
    // Check if plugin can handle this file
    if (plugin->canHandle(filePath)) {
      if (plugin->priority() > highestPriority) {
        bestMatch = plugin.get();
        highestPriority = plugin->priority();
      }
    } else if (!extension.isEmpty() &&
               plugin->supportedExtensions().contains(extension, Qt::CaseInsensitive)) {
      // Check by extension
      if (plugin->priority() > highestPriority) {
        bestMatch = plugin.get();
        highestPriority = plugin->priority();
      }
    }
  }

  return bestMatch;
}

QWidget* ViewerDispatcher::createViewer(
  const QString& filePath,
  QWidget*       parent) const {

  IViewerPlugin* plugin = resolvePlugin(filePath);
  if (plugin) {
    return plugin->createViewer(filePath, parent, m_context);
  }

  // どのビュアーも対応しない場合は Binary ビュアーへフォールバック
  for (const auto& p : m_plugins) {
    if (p->pluginId() == QLatin1String("binary_viewer")) {
      return p->createViewer(filePath, parent, m_context);
    }
  }
  return nullptr;
}

QList<IViewerPlugin*> ViewerDispatcher::allPlugins() const {
  QList<IViewerPlugin*> result;
  result.reserve(m_plugins.size());

  for (const auto& plugin : m_plugins) {
    result.append(plugin.get());
  }

  return result;
}

bool ViewerDispatcher::isExternalPlugin(const QString& pluginId) const {
  for (const PluginRecord& record : m_records) {
    if (record.loaded
        && record.origin == PluginRecord::Origin::External
        && record.pluginId == pluginId) {
      return true;
    }
  }
  return false;
}

void ViewerDispatcher::registerPlugin(std::shared_ptr<IViewerPlugin> plugin,
                                      const QString& filePath,
                                      PluginRecord::Origin origin) {
  // レコード雛形 (同梱公式は origin=Bundled、外部は origin=External)。
  PluginRecord rec;
  rec.origin     = origin;
  rec.filePath   = filePath;
  rec.pluginId   = plugin ? plugin->pluginId()   : QString();
  rec.pluginName = plugin ? plugin->pluginName() : QString();

  if (!plugin) {
    rec.loaded = false;
    rec.errorReason = tr("null plugin instance");
    m_records.append(rec);
    return;
  }

  // 同 ID が既に登録済みなら拒否 (失敗としてレコード)
  for (const auto& existingPlugin : m_plugins) {
    if (existingPlugin->pluginId() == plugin->pluginId()) {
      rec.loaded = false;
      rec.errorReason = tr("duplicate plugin id (already registered)");
      m_records.append(rec);
      Logger::instance().warn(
        QStringLiteral("Plugins: skipping duplicate id '%1'")
          .arg(plugin->pluginId()));
      return;
    }
  }

  // ライフサイクルフック initialize() に PluginContext を渡す。
  // 失敗した場合は登録自体を破棄する (Dispatcher が古い参照を持たない)。
  if (!plugin->initialize(m_context)) {
    rec.loaded = false;
    rec.errorReason = tr("initialize() returned false");
    m_records.append(rec);
    Logger::instance().warn(
      QStringLiteral("Plugins: initialize() failed for '%1'")
        .arg(plugin->pluginId()));
    return;
  }

  rec.supportedExtensions = plugin->supportedExtensions();
  m_plugins.append(plugin);
  rec.loaded = true;
  m_records.append(rec);
  Logger::instance().info(
    QStringLiteral("Plugins: registered '%1' (%2)")
      .arg(plugin->pluginId(),
           origin == PluginRecord::Origin::Bundled ? tr("bundled") : filePath));
}

} // namespace Farman
