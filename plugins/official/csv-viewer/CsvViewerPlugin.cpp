#include "CsvViewerPlugin.h"
#include "viewer/CsvViewerWindow.h"
#include "settings/Settings.h"

namespace Farman {

bool CsvViewerPlugin::initialize(const PluginContext& /*ctx*/) {
  syncPluginFromHostSettings();
  return true;
}

void CsvViewerPlugin::appearanceChanged(const PluginAppearance& /*appearance*/) {
  syncPluginFromHostSettings();
}

QStringList CsvViewerPlugin::supportedExtensions() const {
  return Settings::instance().csvViewerExtensions();
}

QStringList CsvViewerPlugin::supportedMimeTypes() const {
  return {
    QStringLiteral("text/csv"),
    QStringLiteral("text/tab-separated-values")
  };
}

QWidget* CsvViewerPlugin::createViewer(const QString&       filePath,
                                       QWidget*             parent,
                                       const PluginContext& /*ctx*/)
{
  Settings::instance().load();
  auto* window = new CsvViewerWindow(filePath, QString(), parent);
  return window;
}

} // namespace Farman
