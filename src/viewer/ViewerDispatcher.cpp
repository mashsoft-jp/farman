#include "ViewerDispatcher.h"
#include "core/Logger.h"
#include "settings/Settings.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QPalette>
#include <QPluginLoader>
#include <QSet>
#include <limits>

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

bool ViewerDispatcher::isCoreViewerPlugin(const QString& pluginId) {
  // テキスト / 画像 / バイナリ / メディアはフォールバック経路を兼ねる
  // 固定ビュアーとして常に有効にする (ヘッダコメント参照)。
  static const QSet<QString> coreIds = {
    QStringLiteral("text_viewer"),
    QStringLiteral("image_viewer"),
    QStringLiteral("binary_viewer"),
    QStringLiteral("media_viewer"),
  };
  return coreIds.contains(pluginId);
}

void ViewerDispatcher::shutdownPlugins() {
  // initialize() が成功した (= 登録済みの) プラグインだけが対象。
  // 無効化や検証エラーで登録前に弾いたものは initialize していないので
  // shutdown も呼ばない。
  for (const auto& plugin : m_plugins) {
    if (plugin) {
      plugin->shutdown();
    }
  }
  m_plugins.clear();
  // QObject 実体は loader が所有しているので、shutdown 後にアンロードして解放。
  for (const auto& loader : m_pluginLoaders) {
    if (loader) {
      loader->unload();
    }
  }
  m_pluginLoaders.clear();
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
    rec.author = viewerPlugin->author();
    rec.authorUrl = viewerPlugin->authorUrl();
    rec.supportedExtensions = viewerPlugin->supportedExtensions();
    rec.priority = viewerPlugin->priority();
    // ユーザー作成の外部プラグインの優先度は 0〜9999 のみ許可する。
    // 10000 以上は同梱公式プラグイン用の予約域、負の値は不正。
    if (origin == PluginRecord::Origin::External
        && (rec.priority < 0 || rec.priority > 9999)) {
      rec.loaded = false;
      rec.errorReason =
        tr("Invalid priority %1 (external plugins must use 0-9999)")
          .arg(rec.priority);
      m_records.append(rec);
      loader->unload();
      Logger::instance().warn(
        QStringLiteral("Plugins: rejected '%1' (%2): priority %3 out of range")
          .arg(rec.pluginId, fileInfo.fileName())
          .arg(rec.priority));
      continue;
    }
    // 外部プラグインは制作者情報 (author) の提供を必須とする。
    if (origin == PluginRecord::Origin::External
        && rec.author.trimmed().isEmpty()) {
      rec.loaded = false;
      rec.errorReason =
        tr("Missing author information (external plugins must declare author())");
      m_records.append(rec);
      loader->unload();
      Logger::instance().warn(
        QStringLiteral("Plugins: rejected '%1' (%2): author() is empty")
          .arg(rec.pluginId, fileInfo.fileName()));
      continue;
    }
    if (!isCoreViewerPlugin(rec.pluginId)
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

  // 対応プラグインの選択は 2 段階。いずれも優先度が最も高い (= priority 値が
  // 最小の) ものを選び、同点なら先に登録されたものを使う。
  //   1. 拡張子で明示的に対応宣言しているプラグイン (最優先)
  //   2. (1 が無ければ) canHandle が true のプラグイン
  // 拡張子一致を MIME 一致より優先するのは、内容スニッフが当てにならない
  // ケースへの対策。例: HEIC は MP4/MOV と同じ ISO BMFF コンテナなので、
  // 拡張子から MIME を確定できない環境 (Windows 等) では内容スニッフで
  // video/mp4 と誤判定され、media_viewer (高優先度) に静止画が奪われていた。
  IViewerPlugin* bestByExt   = nullptr;  int bestExtPrio   = std::numeric_limits<int>::max();
  IViewerPlugin* bestByMatch = nullptr;  int bestMatchPrio = std::numeric_limits<int>::max();

  for (const auto& plugin : m_plugins) {
    const bool extMatch =
      !extension.isEmpty()
      && plugin->supportedExtensions().contains(extension, Qt::CaseInsensitive);
    if (extMatch) {
      if (plugin->priority() < bestExtPrio) {
        bestByExt = plugin.get();
        bestExtPrio = plugin->priority();
      }
    } else if (plugin->canHandle(filePath)) {
      if (plugin->priority() < bestMatchPrio) {
        bestByMatch = plugin.get();
        bestMatchPrio = plugin->priority();
      }
    }
  }

  return bestByExt ? bestByExt : bestByMatch;
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

IViewerPlugin* ViewerDispatcher::pluginById(const QString& pluginId) const {
  if (pluginId.isEmpty()) return nullptr;
  for (const auto& plugin : m_plugins) {
    if (plugin && plugin->pluginId() == pluginId) {
      return plugin.get();
    }
  }
  return nullptr;
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
  rec.author     = plugin ? plugin->author()     : QString();
  rec.authorUrl  = plugin ? plugin->authorUrl()  : QString();
  rec.priority   = plugin ? plugin->priority()   : -1;

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
