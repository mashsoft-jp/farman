#pragma once

#include "viewer/IViewerPlugin.h"
#include <QObject>

namespace Farman {

class ImageViewerPlugin : public QObject, public IViewerPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID FarmanIViewerPlugin_iid)
  Q_INTERFACES(Farman::IViewerPlugin)

public:
  ImageViewerPlugin() = default;
  ~ImageViewerPlugin() override = default;

  QString pluginId() const override { return "image_viewer"; }
  QString pluginName() const override { return "Image Viewer"; }
  QString author() const override { return QStringLiteral("Mashsoft Inc."); }
  QString authorUrl() const override { return QStringLiteral("https://www.mashsoft.co.jp"); }
  int priority() const override { return 99997; }

  QStringList supportedExtensions() const override {
    return {
      "png", "jpg", "jpeg", "gif", "bmp",
      "svg", "webp", "ico", "tiff", "tif",
      // HEIC / HEIF は MP4/MOV と同じ ISO BMFF コンテナのため、拡張子で
      // 明示宣言しておかないと内容スニッフで動画扱いされ media_viewer に
      // 奪われる (resolvePlugin が拡張子一致を最優先するので静止画へ回る)。
      "heic", "heif"
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
      "image/tiff",
      "image/heic",
      "image/heif"
    };
  }

  bool initialize(const PluginContext& ctx) override;
  void appearanceChanged(const PluginAppearance& appearance) override;

  bool hasSettings() const override { return true; }
  IPluginSettingsPage* createSettingsPage(QWidget* parent) override;

  QWidget* createViewer(const QString&       filePath,
                        QWidget*             parent,
                        const PluginContext& ctx) override;
};

} // namespace Farman
