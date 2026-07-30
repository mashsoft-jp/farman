// farman 3D モデルビュアー PoC。
// Assimp で読み込んだモデルを ModelView (QOpenGLWidget) で表示する検証用の
// 単体アプリ。使い方: farman_model_poc <path/to/model.fbx>

#include "viewer/ModelView.h"

#include <QApplication>
#include <QImage>
#include <QStringList>
#include <QSurfaceFormat>

// 使い方:
//   farman_model_poc <model file>              … ウィンドウ表示
//   farman_model_poc <model file> --shot <png> … オフスクリーンで PNG 出力して終了
int main(int argc, char** argv) {
  QApplication app(argc, argv);

  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setDepthBufferSize(24);
  fmt.setSamples(4);  // MSAA
  QSurfaceFormat::setDefaultFormat(fmt);

  const QStringList args = app.arguments();
  QString modelPath, shotPath;
  for (int i = 1; i < args.size(); ++i) {
    if (args[i] == QLatin1String("--shot") && i + 1 < args.size()) {
      shotPath = args[++i];
    } else if (modelPath.isEmpty()) {
      modelPath = args[i];
    }
  }
  if (modelPath.isEmpty()) {
    qWarning("usage: farman_model_poc <model file> [--shot <out.png>]");
    return 2;
  }

  auto* view = new Farman::ModelView;
  view->resize(960, 720);

  QString err;
  if (!view->loadModel(modelPath, &err)) {
    qWarning("load failed: %s", qPrintable(err));
    return 1;
  }
  qInfo("loaded: %s", qPrintable(view->summary()));
  view->setWindowTitle(QStringLiteral("farman 3D PoC — ") + view->summary());

  if (!shotPath.isEmpty()) {
    // オフスクリーンで 1 フレーム描画して PNG 保存 (画面表示不要)
    const QImage img = view->grabFramebuffer();
    const bool ok = img.save(shotPath);
    qInfo("shot %s: %s (%dx%d)", ok ? "saved" : "FAILED", qPrintable(shotPath),
          img.width(), img.height());
    return ok ? 0 : 1;
  }

  view->show();
  return app.exec();
}
