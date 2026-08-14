#pragma once

#include "viewer/IViewerPlugin.h"
#include "settings/Settings.h"
#include <QObject>
#include <QCoreApplication>

namespace Farman {

class TextViewerPlugin : public QObject, public IViewerPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID FarmanIViewerPlugin_iid)
  Q_INTERFACES(Farman::IViewerPlugin)

public:
  TextViewerPlugin() = default;
  ~TextViewerPlugin() override = default;

  QString pluginId() const override { return "text_viewer"; }
  QString pluginName() const override { return QCoreApplication::translate("ViewerNames", "Text Viewer"); }
  QList<ViewerCommandDef> shortcutCommands() const override {
    return viewerCommandsForViewer(QStringLiteral("text"));
  }
  QString author() const override { return QStringLiteral("Mashsoft Inc."); }
  QString authorUrl() const override { return QStringLiteral("https://www.mashsoft.co.jp"); }
  int priority() const override { return 99998; }

  // 対象ファイルパターン。既定一覧は Settings が持ち、設定 → ビュアーの
  // 詳細ダイアログから変更できる。ここでコード固定の一覧を返していた頃は、
  // 設定で増減しても本流の判定 (ViewerDispatcher::resolvePlugin) に効かなかった。
  QStringList supportedExtensions() const override {
    return Settings::instance().textViewerExtensions();
  }

  QStringList supportedMimeTypes() const override {
    return {
      "text/plain",
      "text/x-c",
      "text/x-c++",
      "text/x-python",
      "text/x-javascript",
      "text/html",
      "text/css",
      "application/json",
      "application/xml"
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
