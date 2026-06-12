#pragma once

#include "viewer/IViewerPlugin.h"
#include <QObject>

namespace Farman {

// 他のビュアーがどれもマッチしないファイルを開くためのフォールバックビュアー。
// 自身は拡張子・MIME のいずれにもマッチしない (canHandle 常に false) ので、
// `ViewerDispatcher::resolvePlugin` 内では選ばれず、フォールバック経路でのみ使われる。
class BinaryViewerPlugin : public QObject, public IViewerPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID FarmanIViewerPlugin_iid)
  Q_INTERFACES(Farman::IViewerPlugin)

public:
  BinaryViewerPlugin() = default;
  ~BinaryViewerPlugin() override = default;

  QString pluginId()   const override { return QStringLiteral("binary_viewer"); }
  QString pluginName() const override { return QStringLiteral("Binary Viewer"); }
  int     priority()   const override { return 99999; }

  QStringList supportedExtensions() const override { return {}; }
  QStringList supportedMimeTypes()  const override { return {}; }

  bool canHandle(const QString& /*filePath*/) const override { return false; }

  bool initialize(const PluginContext& ctx) override;
  void appearanceChanged(const PluginAppearance& appearance) override;

  QWidget* createViewer(const QString&       filePath,
                        QWidget*             parent,
                        const PluginContext& ctx) override;
};

} // namespace Farman
