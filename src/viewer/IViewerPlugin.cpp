#include "IViewerPlugin.h"

#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QtGlobal>

#include "core/Logger.h"
#include "settings/Settings.h"
#include "utils/MediaMatchers.h"

namespace Farman {

QString IViewerPlugin::version() const {
  // 既定はビルド時に埋め込んだ FARMAN_VERSION。同梱公式プラグインは farman
  // 本体と同じ版数を報告する。FARMAN_VERSION が未定義のビルド (外部プラグイン
  // が自前でこの .cpp を含めていて版を渡していない等) では空を返す。
#ifdef FARMAN_VERSION
  return QStringLiteral(QT_STRINGIFY(FARMAN_VERSION));
#else
  return {};
#endif
}

void syncPluginFromHostSettings() {
  Settings& settings = Settings::instance();
  settings.load();
  Logger::instance().setFileOutput(settings.logToFile(),
                                   settings.logDirectory(),
                                   settings.logRetentionDays());
}

QStringList IViewerPlugin::effectiveFilePatterns() const {
  const QStringList overridden =
    Settings::instance().viewerFilePatternsFor(pluginId());
  return overridden.isEmpty() ? supportedExtensions() : overridden;
}

bool IViewerPlugin::canHandle(const QString& filePath) const {
  const QFileInfo fileInfo(filePath);

  // 対応宣言は「拡張子の完全一致リスト」ではなくファイルパターンのリストと
  // して扱う。"mp4" のような拡張子だけの書き方に加えて "*.tar.gz" (複合
  // 拡張子) や "Makefile" (拡張子無し) も書ける
  // (MediaMatchers::fileNameMatches のコメント参照)。
  if (MediaMatchers::fileNameMatches(effectiveFilePatterns(), fileInfo.fileName())) {
    return true;
  }

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
