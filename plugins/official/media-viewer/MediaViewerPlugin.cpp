#include "MediaViewerPlugin.h"
#include "MediaViewerSettingsPage.h"
#include "viewer/MediaViewerWindow.h"
#include "settings/Settings.h"

namespace Farman {

IPluginSettingsPage* MediaViewerPlugin::createSettingsPage(QWidget* parent) {
  return new MediaViewerSettingsPage(parent);
}

bool MediaViewerPlugin::initialize(const PluginContext& /*ctx*/) {
  syncPluginFromHostSettings();
  return true;
}

void MediaViewerPlugin::appearanceChanged(const PluginAppearance& /*appearance*/) {
  syncPluginFromHostSettings();
}

QStringList MediaViewerPlugin::supportedExtensions() const {
  // 対象拡張子 (動画 + 音声)。実際のデコード可否はプラットフォームの
  // Qt Multimedia バックエンド依存。既定一覧は Settings が持ち、設定ページ
  // (Plugins → 詳細) から変更できる。
  return Settings::instance().mediaViewerExtensions();
}

QStringList MediaViewerPlugin::supportedMimeTypes() const {
  return {
    QStringLiteral("video/mp4"),
    QStringLiteral("video/quicktime"),
    QStringLiteral("video/webm"),
    QStringLiteral("video/x-matroska"),
    QStringLiteral("video/x-msvideo"),
    QStringLiteral("video/x-ms-wmv"),
    QStringLiteral("video/mpeg"),
    QStringLiteral("video/mp2t"),
    QStringLiteral("audio/mpeg"),
    QStringLiteral("audio/mp4"),
    QStringLiteral("audio/flac"),
    QStringLiteral("audio/ogg"),
    QStringLiteral("audio/x-wav"),
    QStringLiteral("audio/aac"),
    QStringLiteral("audio/x-ms-wma"),
  };
}

QWidget* MediaViewerPlugin::createViewer(const QString&       filePath,
                                         QWidget*             parent,
                                         const PluginContext& /*ctx*/)
{
  Settings::instance().load();
  auto* window = new MediaViewerWindow(filePath, QString(), parent);
  return window;
}

} // namespace Farman
