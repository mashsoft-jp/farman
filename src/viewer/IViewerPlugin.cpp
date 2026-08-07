#include "IViewerPlugin.h"

#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QtGlobal>

#include "core/Logger.h"
#include "settings/Settings.h"
#include "keybinding/ViewerKeyBindingManager.h"

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
  // ビュアーのショートカット割り当ても本体側で変わり得るので再読込する
  // (media 等プラグイン内ビュアーが最新の割当で動くように)。
  ViewerKeyBindingManager::instance().loadFromSettings();
}

bool IViewerPlugin::canHandle(const QString& filePath) const {
  QFileInfo fileInfo(filePath);
  QString extension = fileInfo.suffix().toLower();

  if (!extension.isEmpty() &&
      supportedExtensions().contains(extension, Qt::CaseInsensitive)) {
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
