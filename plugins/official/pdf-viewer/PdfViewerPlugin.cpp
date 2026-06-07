#include "PdfViewerPlugin.h"
#include "viewer/PdfViewerWindow.h"
#include "settings/Settings.h"

namespace Farman {

QStringList PdfViewerPlugin::supportedExtensions() const {
  return Settings::instance().pdfViewerExtensions();
}

QStringList PdfViewerPlugin::supportedMimeTypes() const {
  return {QStringLiteral("application/pdf")};
}

QWidget* PdfViewerPlugin::createViewer(const QString&       filePath,
                                       QWidget*             parent,
                                       const PluginContext& /*ctx*/)
{
  auto* window = new PdfViewerWindow(filePath, QString(), parent);
  return window;
}

} // namespace Farman
