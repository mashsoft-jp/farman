#include "ViewerDispatcher.h"
#include "TextViewerPlugin.h"
#include "ImageViewerPlugin.h"
#include "BinaryViewerPlugin.h"
#include "core/Logger.h"
#include <QDebug>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QPluginLoader>

namespace Farman {

ViewerDispatcher& ViewerDispatcher::instance() {
  static ViewerDispatcher instance;
  return instance;
}

ViewerDispatcher::ViewerDispatcher(QObject* parent) : QObject(parent) {
}

void ViewerDispatcher::registerBuiltins() {
  // Register built-in viewers
  registerPlugin(std::make_shared<TextViewerPlugin>());
  registerPlugin(std::make_shared<ImageViewerPlugin>());
  // フォールバック (canHandle が false なので resolvePlugin では選ばれない)
  registerPlugin(std::make_shared<BinaryViewerPlugin>());
}

void ViewerDispatcher::loadPlugins(const QDir& pluginDir) {
  // Load external plugins from directory
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
    rec.origin   = PluginRecord::Origin::External;
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
    // QPluginLoader が QObject (= IViewerPlugin の実体) のライフタイムを
    // 管理するので、shared_ptr 側は delete しない deleter を使う。
    // registerPlugin が成功・失敗ともに m_records に最終結果を追記する。
    registerPlugin(std::shared_ptr<IViewerPlugin>(viewerPlugin, [](IViewerPlugin*){}),
                   /*filePath=*/rec.filePath);
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
    QStringLiteral("Plugins: %1 loaded, %2 failed from %3")
      .arg(loadedCount).arg(failedCount).arg(pluginDir.absolutePath()));
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

void ViewerDispatcher::registerPlugin(std::shared_ptr<IViewerPlugin> plugin,
                                      const QString& filePath) {
  // レコード雛形 (組み込みは origin=Builtin / filePath 空、外部は origin=External)。
  PluginRecord rec;
  rec.origin     = filePath.isEmpty() ? PluginRecord::Origin::Builtin
                                       : PluginRecord::Origin::External;
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

  m_plugins.append(plugin);
  rec.loaded = true;
  m_records.append(rec);
  Logger::instance().info(
    QStringLiteral("Plugins: registered '%1' (%2)")
      .arg(plugin->pluginId(),
           filePath.isEmpty() ? tr("built-in") : filePath));
}

bool IViewerPlugin::canHandle(const QString& filePath) const {
  QFileInfo fileInfo(filePath);
  QString extension = fileInfo.suffix().toLower();

  // Check by extension
  if (!extension.isEmpty() &&
      supportedExtensions().contains(extension, Qt::CaseInsensitive)) {
    return true;
  }

  // Check by MIME type
  QMimeDatabase mimeDb;
  QMimeType mimeType = mimeDb.mimeTypeForFile(filePath);

  for (const QString& supportedMime : supportedMimeTypes()) {
    if (mimeType.inherits(supportedMime)) {
      return true;
    }
  }

  return false;
}

} // namespace Farman
