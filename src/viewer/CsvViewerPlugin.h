#pragma once

#include "IViewerPlugin.h"

namespace Farman {

class CsvViewerPlugin : public IViewerPlugin {
public:
  CsvViewerPlugin() = default;
  ~CsvViewerPlugin() override = default;

  QString pluginId() const override { return QStringLiteral("csv_viewer"); }
  QString pluginName() const override { return QStringLiteral("CSV/TSV Viewer"); }
  int priority() const override { return 200; }

  QStringList supportedExtensions() const override;
  QStringList supportedMimeTypes() const override;

  QWidget* createViewer(const QString&       filePath,
                        QWidget*             parent,
                        const PluginContext& ctx) override;
};

} // namespace Farman
