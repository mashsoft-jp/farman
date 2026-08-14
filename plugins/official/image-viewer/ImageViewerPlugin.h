#pragma once

#include "viewer/IViewerPlugin.h"
#include "settings/Settings.h"
#include <QObject>
#include <QCoreApplication>

namespace Farman {

class ImageViewerPlugin : public QObject, public IViewerPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID FarmanIViewerPlugin_iid)
  Q_INTERFACES(Farman::IViewerPlugin)

public:
  ImageViewerPlugin() = default;
  ~ImageViewerPlugin() override = default;

  QString pluginId() const override { return "image_viewer"; }
  QString pluginName() const override { return QCoreApplication::translate("ViewerNames", "Image Viewer"); }
  QList<ViewerCommandDef> shortcutCommands() const override {
    return viewerCommandsForViewer(QStringLiteral("image"));
  }
  QString author() const override { return QStringLiteral("Mashsoft Inc."); }
  QString authorUrl() const override { return QStringLiteral("https://www.mashsoft.co.jp"); }
  int priority() const override { return 99997; }

  // 対象ファイルパターン。既定一覧 (png / jpg / ... に加えて、合成プレビュー
  // 対応の psd と、動画と同じ ISO BMFF で内容スニッフが当てにならない
  // heic / heif) は Settings が持ち、設定 → ビュアーの詳細ダイアログから
  // 変更できる。ここでコード固定の一覧を返していた頃は、設定で増減しても
  // 本流の判定 (ViewerDispatcher::resolvePlugin) に効かなかった。
  QStringList supportedExtensions() const override {
    return Settings::instance().imageViewerExtensions();
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
      "image/vnd.adobe.photoshop",
      "image/heic",
      "image/heif"
    };
  }

  bool initialize(const PluginContext& ctx) override;
  void appearanceChanged(const PluginAppearance& appearance) override;

  bool hasSettings() const override { return true; }
  IPluginSettingsPage* createSettingsPage(QWidget* parent) override;
  bool managesOwnExtensions() const override { return true; }

  QWidget* createViewer(const QString&       filePath,
                        QWidget*             parent,
                        const PluginContext& ctx) override;
};

} // namespace Farman
