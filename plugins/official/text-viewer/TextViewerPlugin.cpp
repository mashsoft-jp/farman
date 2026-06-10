#include "TextViewerPlugin.h"
#include "settings/Settings.h"
#include "viewer/TextViewerWindow.h"

namespace Farman {

bool TextViewerPlugin::initialize(const PluginContext& /*ctx*/) {
  syncPluginFromHostSettings();
  return true;
}

void TextViewerPlugin::appearanceChanged(const PluginAppearance& /*appearance*/) {
  syncPluginFromHostSettings();
}

QWidget* TextViewerPlugin::createViewer(const QString&       filePath,
                                        QWidget*             parent,
                                        const PluginContext& /*ctx*/)
{
  Settings::instance().load();
  auto* window = new TextViewerWindow(filePath, QString(), parent);
  return window;
}

} // namespace Farman
