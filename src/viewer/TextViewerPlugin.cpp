#include "TextViewerPlugin.h"
#include "TextViewerWindow.h"

namespace Farman {

QWidget* TextViewerPlugin::createViewer(const QString&       filePath,
                                        QWidget*             parent,
                                        const PluginContext& /*ctx*/)
{
  auto* window = new TextViewerWindow(filePath, QString(), parent);
  return window;
}

} // namespace Farman
