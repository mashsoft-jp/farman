#include "PdfViewerPlugin.h"
#include "PdfViewerSettingsPage.h"
#include "viewer/PdfViewerWindow.h"
#include "settings/Settings.h"

namespace Farman {

IPluginSettingsPage* PdfViewerPlugin::createSettingsPage(QWidget* parent) {
  return new PdfViewerSettingsPage(parent);
}

bool PdfViewerPlugin::initialize(const PluginContext& /*ctx*/) {
  syncPluginFromHostSettings();
  return true;
}

void PdfViewerPlugin::appearanceChanged(const PluginAppearance& /*appearance*/) {
  syncPluginFromHostSettings();
}

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
  Settings::instance().load();
  auto* window = new PdfViewerWindow(filePath, QString(), parent);
  return window;
}

} // namespace Farman
