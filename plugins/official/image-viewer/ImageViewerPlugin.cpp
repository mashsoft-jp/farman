#include "ImageViewerPlugin.h"
#include "ImageViewerSettingsPage.h"
#include "settings/Settings.h"
#include "viewer/ImageViewerWindow.h"

namespace Farman {

IPluginSettingsPage* ImageViewerPlugin::createSettingsPage(QWidget* parent) {
  return new ImageViewerSettingsPage(parent);
}

bool ImageViewerPlugin::initialize(const PluginContext& /*ctx*/) {
  syncPluginFromHostSettings();
  return true;
}

void ImageViewerPlugin::appearanceChanged(const PluginAppearance& /*appearance*/) {
  syncPluginFromHostSettings();
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
