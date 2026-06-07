#pragma once

#include "viewer/IViewerPlugin.h"

namespace Farman {

// 他のビュアーがどれもマッチしないファイルを開くためのフォールバックビュアー。
// 自身は拡張子・MIME のいずれにもマッチしない (canHandle 常に false) ので、
// `ViewerDispatcher::resolvePlugin` 内では選ばれず、フォールバック経路でのみ使われる。
class BinaryViewerPlugin : public IViewerPlugin {
public:
  BinaryViewerPlugin() = default;
  ~BinaryViewerPlugin() override = default;

  QString pluginId()   const override { return QStringLiteral("binary_viewer"); }
  QString pluginName() const override { return QStringLiteral("Binary Viewer"); }
  int     priority()   const override { return -1; }

  QStringList supportedExtensions() const override { return {}; }
  QStringList supportedMimeTypes()  const override { return {}; }

  bool canHandle(const QString& /*filePath*/) const override { return false; }

  QWidget* createViewer(const QString&       filePath,
                        QWidget*             parent,
                        const PluginContext& ctx) override;
};

} // namespace Farman
