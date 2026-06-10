#include "IViewerPlugin.h"

#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>

#include "core/Logger.h"
#include "settings/Settings.h"

namespace Farman {

void syncPluginFromHostSettings() {
  Settings& settings = Settings::instance();
  settings.load();
  Logger::instance().setFileOutput(settings.logToFile(),
                                   settings.logDirectory(),
                                   settings.logRetentionDays());
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
