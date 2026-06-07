#pragma once

#include "viewer/IViewerPlugin.h"
#include <QObject>

namespace Farman {

class PdfViewerPlugin : public QObject, public IViewerPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "com.farman.IViewerPlugin/1.0")
  Q_INTERFACES(Farman::IViewerPlugin)

public:
  PdfViewerPlugin() = default;
  ~PdfViewerPlugin() override = default;

  QString pluginId() const override { return QStringLiteral("pdf_viewer"); }
  QString pluginName() const override { return QStringLiteral("PDF Viewer"); }
  int priority() const override { return 200; }

  QStringList supportedExtensions() const override;
  QStringList supportedMimeTypes() const override;

  bool initialize(const PluginContext& ctx) override;
  void appearanceChanged(const PluginAppearance& appearance) override;

  QWidget* createViewer(const QString&       filePath,
                        QWidget*             parent,
                        const PluginContext& ctx) override;
};

} // namespace Farman
