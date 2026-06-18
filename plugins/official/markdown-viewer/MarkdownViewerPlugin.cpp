#include "MarkdownViewerPlugin.h"
#include "MarkdownViewerSettingsPage.h"
#include "viewer/MarkdownViewerWindow.h"
#include "settings/Settings.h"

namespace Farman {

IPluginSettingsPage* MarkdownViewerPlugin::createSettingsPage(QWidget* parent) {
  return new MarkdownViewerSettingsPage(parent);
}

bool MarkdownViewerPlugin::initialize(const PluginContext& /*ctx*/) {
  syncPluginFromHostSettings();
  return true;
}

void MarkdownViewerPlugin::appearanceChanged(const PluginAppearance& /*appearance*/) {
  syncPluginFromHostSettings();
}

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
  Settings::instance().load();
  auto* window = new MarkdownViewerWindow(filePath, QString(), parent);
  return window;
}

} // namespace Farman
