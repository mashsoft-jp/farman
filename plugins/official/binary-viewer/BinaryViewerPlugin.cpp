#include "BinaryViewerPlugin.h"
#include "viewer/BinaryViewerWindow.h"

namespace Farman {

QWidget* BinaryViewerPlugin::createViewer(const QString&       filePath,
                                          QWidget*             parent,
                                          const PluginContext& /*ctx*/)
{
  auto* window = new BinaryViewerWindow(filePath, QString(), parent);
  return window;
}

} // namespace Farman
