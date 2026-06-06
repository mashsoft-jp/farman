#pragma once

#include "IViewerPlugin.h"

namespace Farman {

class PdfViewerPlugin : public IViewerPlugin {
public:
  PdfViewerPlugin() = default;
  ~PdfViewerPlugin() override = default;

  QString pluginId() const override { return QStringLiteral("pdf_viewer"); }
  QString pluginName() const override { return QStringLiteral("PDF Viewer"); }
  int priority() const override { return 200; }

  QStringList supportedExtensions() const override;
  QStringList supportedMimeTypes() const override;

  QWidget* createViewer(const QString&       filePath,
                        QWidget*             parent,
                        const PluginContext& ctx) override;
};

} // namespace Farman
