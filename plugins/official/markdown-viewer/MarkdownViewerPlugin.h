#pragma once

#include "viewer/IViewerPlugin.h"

namespace Farman {

class MarkdownViewerPlugin : public IViewerPlugin {
public:
  MarkdownViewerPlugin() = default;
  ~MarkdownViewerPlugin() override = default;

  QString pluginId() const override { return QStringLiteral("markdown_viewer"); }
  QString pluginName() const override { return QStringLiteral("Markdown Viewer"); }
  int priority() const override { return 200; }

  QStringList supportedExtensions() const override;
  QStringList supportedMimeTypes() const override;

  QWidget* createViewer(const QString&       filePath,
                        QWidget*             parent,
                        const PluginContext& ctx) override;
};

} // namespace Farman
