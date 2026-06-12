#pragma once

#include "viewer/IViewerPlugin.h"
#include <QObject>

namespace Farman {

class TextViewerPlugin : public QObject, public IViewerPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID FarmanIViewerPlugin_iid)
  Q_INTERFACES(Farman::IViewerPlugin)

public:
  TextViewerPlugin() = default;
  ~TextViewerPlugin() override = default;

  QString pluginId() const override { return "text_viewer"; }
  QString pluginName() const override { return "Text Viewer"; }
  int priority() const override { return 99998; }

  QStringList supportedExtensions() const override {
    return {
      "txt", "log",
      "cpp", "h", "hpp", "c", "cc", "cxx",
      "py", "js", "ts", "java", "cs",
      "html", "htm", "css", "json", "xml",
      "sh", "bash", "zsh", "fish",
      "rs", "go", "rb", "php", "pl", "pm",
      "yaml", "yml", "toml", "ini", "conf", "cfg"
    };
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

  QWidget* createViewer(const QString&       filePath,
                        QWidget*             parent,
                        const PluginContext& ctx) override;
};

} // namespace Farman
