#include "ImageViewerPlugin.h"
#include "viewer/ImageViewerWindow.h"

namespace Farman {

QWidget* ImageViewerPlugin::createViewer(const QString&       filePath,
                                         QWidget*             parent,
                                         const PluginContext& /*ctx*/)
{
  auto* window = new ImageViewerWindow(filePath, QString(), parent);
  return window;
}

} // namespace Farman
