#pragma once

#include "viewer/IViewerPlugin.h"
#include <QObject>

namespace Farman {

class CsvViewerPlugin : public QObject, public IViewerPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID FarmanIViewerPlugin_iid)
  Q_INTERFACES(Farman::IViewerPlugin)

public:
  CsvViewerPlugin() = default;
  ~CsvViewerPlugin() override = default;

  QString pluginId() const override { return QStringLiteral("csv_viewer"); }
  QString pluginName() const override { return QStringLiteral("CSV/TSV Viewer"); }
  int priority() const override { return 10000; }

  QStringList supportedExtensions() const override;
  QStringList supportedMimeTypes() const override;

  bool initialize(const PluginContext& ctx) override;
  void appearanceChanged(const PluginAppearance& appearance) override;

  QWidget* createViewer(const QString&       filePath,
                        QWidget*             parent,
                        const PluginContext& ctx) override;
};

} // namespace Farman
