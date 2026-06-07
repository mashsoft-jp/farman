#pragma once

#include "viewer/IViewerPlugin.h"

namespace Farman {

class ImageViewerPlugin : public IViewerPlugin {
public:
  ImageViewerPlugin() = default;
  ~ImageViewerPlugin() override = default;

  QString pluginId() const override { return "image_viewer"; }
  QString pluginName() const override { return "Image Viewer"; }
  int priority() const override { return 100; }

  QStringList supportedExtensions() const override {
    return {
      "png", "jpg", "jpeg", "gif", "bmp",
      "svg", "webp", "ico", "tiff", "tif"
    };
  }

  QStringList supportedMimeTypes() const override {
    return {
      "image/png",
      "image/jpeg",
      "image/gif",
      "image/bmp",
      "image/svg+xml",
      "image/webp",
      "image/x-icon",
      "image/tiff"
    };
  }

  QWidget* createViewer(const QString&       filePath,
                        QWidget*             parent,
                        const PluginContext& ctx) override;
};

} // namespace Farman
