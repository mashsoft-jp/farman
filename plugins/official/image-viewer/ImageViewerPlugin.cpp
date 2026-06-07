#include "ImageViewerPlugin.h"
#include "settings/Settings.h"
#include "viewer/ImageViewerWindow.h"

namespace Farman {

bool ImageViewerPlugin::initialize(const PluginContext& /*ctx*/) {
  Settings::instance().load();
  return true;
}

void ImageViewerPlugin::appearanceChanged(const PluginAppearance& /*appearance*/) {
  Settings::instance().load();
}

QWidget* ImageViewerPlugin::createViewer(const QString&       filePath,
                                         QWidget*             parent,
                                         const PluginContext& /*ctx*/)
{
  Settings::instance().load();
  auto* window = new ImageViewerWindow(filePath, QString(), parent);
  return window;
}

} // namespace Farman
