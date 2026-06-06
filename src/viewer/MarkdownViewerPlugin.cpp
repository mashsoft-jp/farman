#include "MarkdownViewerPlugin.h"
#include "MarkdownViewerWindow.h"
#include "settings/Settings.h"

namespace Farman {

QStringList MarkdownViewerPlugin::supportedExtensions() const {
  return Settings::instance().markdownViewerExtensions();
}

QStringList MarkdownViewerPlugin::supportedMimeTypes() const {
  return {
    QStringLiteral("text/markdown"),
    QStringLiteral("text/x-markdown")
  };
}

QWidget* MarkdownViewerPlugin::createViewer(const QString&       filePath,
                                            QWidget*             parent,
                                            const PluginContext& /*ctx*/)
{
  auto* window = new MarkdownViewerWindow(filePath, QString(), parent);
  return window;
}

} // namespace Farman
