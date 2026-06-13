#pragma once

#include "viewer/IViewerPlugin.h"
#include <QObject>

namespace Farman {

class MediaViewerPlugin : public QObject, public IViewerPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID FarmanIViewerPlugin_iid)
  Q_INTERFACES(Farman::IViewerPlugin)

public:
  MediaViewerPlugin() = default;
  ~MediaViewerPlugin() override = default;

  QString pluginId() const override { return QStringLiteral("media_viewer"); }
  QString pluginName() const override { return QStringLiteral("Media Viewer"); }
  QString author() const override { return QStringLiteral("Mashsoft Inc."); }
  QString authorUrl() const override { return QStringLiteral("https://www.mashsoft.co.jp"); }
  int priority() const override { return 99996; }

  QStringList supportedExtensions() const override;
  QStringList supportedMimeTypes() const override;

  bool initialize(const PluginContext& ctx) override;
  void appearanceChanged(const PluginAppearance& appearance) override;

  QWidget* createViewer(const QString&       filePath,
                        QWidget*             parent,
                        const PluginContext& ctx) override;
};

} // namespace Farman
