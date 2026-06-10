#include "MediaViewerPlugin.h"
#include "viewer/MediaViewerWindow.h"
#include "core/Logger.h"
#include "settings/Settings.h"

namespace Farman {

namespace {
// Settings を再ロードし、プラグイン側 Logger をアプリ本体と同じ設定に揃える。
// プラグイン dylib は Settings / Logger シングルトンを自前で持つ (本体とは別
// インスタンス) ため、これが無いと createViewer 後の非同期ロード結末
// (logViewerLoadResult) がどこにも記録されない。
// 起動時 (initialize) と設定変更時 (appearanceChanged が settingsChanged の
// 通知経路を兼ねる) の両方から呼んで、ログ出力 ON/OFF / ディレクトリ /
// 保持日数の変更にも追従させる。
void syncFromHostSettings() {
  Settings& settings = Settings::instance();
  settings.load();
  Logger::instance().setFileOutput(settings.logToFile(),
                                   settings.logDirectory(),
                                   settings.logRetentionDays());
}
} // namespace

bool MediaViewerPlugin::initialize(const PluginContext& /*ctx*/) {
  syncFromHostSettings();
  return true;
}

void MediaViewerPlugin::appearanceChanged(const PluginAppearance& /*appearance*/) {
  syncFromHostSettings();
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
