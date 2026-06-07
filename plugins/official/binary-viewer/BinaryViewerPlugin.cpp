#include "BinaryViewerPlugin.h"
#include "settings/Settings.h"
#include "viewer/BinaryViewerWindow.h"

namespace Farman {

bool BinaryViewerPlugin::initialize(const PluginContext& /*ctx*/) {
  Settings::instance().load();
  return true;
}

void BinaryViewerPlugin::appearanceChanged(const PluginAppearance& /*appearance*/) {
  Settings::instance().load();
}

QWidget* BinaryViewerPlugin::createViewer(const QString&       filePath,
                                          QWidget*             parent,
                                          const PluginContext& /*ctx*/)
{
  Settings::instance().load();
  auto* window = new BinaryViewerWindow(filePath, QString(), parent);
  return window;
}

} // namespace Farman
