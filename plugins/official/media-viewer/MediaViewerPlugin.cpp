#include "MediaViewerPlugin.h"
#include "viewer/MediaViewerWindow.h"
#include "core/Logger.h"
#include "settings/Settings.h"

namespace Farman {

bool MediaViewerPlugin::initialize(const PluginContext& /*ctx*/) {
  Settings& settings = Settings::instance();
  settings.load();
  // プラグイン dylib は Logger シングルトンを自前で持つ (アプリ本体とは別
  // インスタンス) ため、本体と同じ設定でファイル出力を有効化しておく。
  // これが無いと createViewer 後の非同期ロード結末 (logViewerLoadResult) が
  // どこにも記録されない。
  Logger::instance().setFileOutput(settings.logToFile(),
                                   settings.logDirectory(),
                                   settings.logRetentionDays());
  return true;
}

void MediaViewerPlugin::appearanceChanged(const PluginAppearance& /*appearance*/) {
  Settings::instance().load();
}

QStringList MediaViewerPlugin::supportedExtensions() const {
  // SPEC の対象拡張子 (動画 + 音声)。実際のデコード可否はプラットフォームの
  // Qt Multimedia バックエンド依存。ユーザーの上書きは Settings →
  // Viewer Associations で行う。
  return {
    // 動画
    QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("m4v"),
    QStringLiteral("webm"), QStringLiteral("avi"), QStringLiteral("mkv"),
    // 音声
    QStringLiteral("wav"), QStringLiteral("mp3"), QStringLiteral("m4a"),
    QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("aac"),
  };
}

QStringList MediaViewerPlugin::supportedMimeTypes() const {
  return {
    QStringLiteral("video/mp4"),
    QStringLiteral("video/quicktime"),
    QStringLiteral("video/webm"),
    QStringLiteral("video/x-matroska"),
    QStringLiteral("video/x-msvideo"),
    QStringLiteral("audio/mpeg"),
    QStringLiteral("audio/mp4"),
    QStringLiteral("audio/flac"),
    QStringLiteral("audio/ogg"),
    QStringLiteral("audio/x-wav"),
    QStringLiteral("audio/aac"),
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
