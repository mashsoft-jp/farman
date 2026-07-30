// farman 3D モデルビュアー PoC。
// Assimp で読み込んだモデルを ModelView (QOpenGLWidget) で表示する検証用アプリ。
//   farman_model_poc <model file>                     … ウィンドウ表示
//   farman_model_poc <model file> --shot <png>        … オフスクリーンで PNG 出力
//   farman_model_poc <model file> [--shot ..] --no-texture … テクスチャ OFF で

#include "viewer/ModelView.h"

#include <QAction>
#include <QApplication>
#include <QImage>
#include <QMainWindow>
#include <QStatusBar>
#include <QStringList>
#include <QSurfaceFormat>
#include <QToolBar>

namespace {

// テクスチャの状態を人間可読の 1 行にする（外部の場合は解決したパスを示す）。
QString textureInfo(const Farman::ModelView& v) {
  if (!v.hasTexture()) return QStringLiteral("テクスチャ: なし");
  QString s;
  if (v.textureEmbedded()) {
    s = QStringLiteral("テクスチャ: 埋め込み");
  } else if (v.textureResolved()) {
    s = QStringLiteral("テクスチャ: %1").arg(v.textureResolvedPath());
  } else {
    s = QStringLiteral("テクスチャ: %1 (見つからず)").arg(v.textureRecordedPath());
  }
  if (!v.hasUV()) s += QStringLiteral("  ※メッシュに UV なし");
  return s;
}

} // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setDepthBufferSize(24);
  fmt.setSamples(4);
  QSurfaceFormat::setDefaultFormat(fmt);

  QString modelPath, shotPath;
  bool    noTexture = false;
  const QStringList args = app.arguments();
  for (int i = 1; i < args.size(); ++i) {
    if (args[i] == QLatin1String("--shot") && i + 1 < args.size()) {
      shotPath = args[++i];
    } else if (args[i] == QLatin1String("--no-texture")) {
      noTexture = true;
    } else if (modelPath.isEmpty()) {
      modelPath = args[i];
    }
  }
  if (modelPath.isEmpty()) {
    qWarning("usage: farman_model_poc <model file> [--shot <out.png>] [--no-texture]");
    return 2;
  }

  auto* view = new Farman::ModelView;
  view->resize(1000, 780);

  QString err;
  if (!view->loadModel(modelPath, &err)) {
    qWarning("load failed: %s", qPrintable(err));
    return 1;
  }
  view->setTextureEnabled(!noTexture);

  // 読み込み結果とテクスチャの紐付け（記録パス / 解決パス）をコンソールにも出す。
  qInfo("loaded: %s", qPrintable(view->summary()));
  qInfo("%s", qPrintable(textureInfo(*view)));
  if (view->hasTexture() && !view->textureEmbedded()) {
    qInfo("  FBX 記録パス: %s", qPrintable(view->textureRecordedPath()));
    qInfo("  解決パス    : %s",
          qPrintable(view->textureResolved() ? view->textureResolvedPath()
                                             : QStringLiteral("(見つからず)")));
  }

  if (!shotPath.isEmpty()) {
    const QImage img = view->grabFramebuffer();
    const bool   ok  = img.save(shotPath);
    qInfo("shot %s: %s (%dx%d)", ok ? "saved" : "FAILED", qPrintable(shotPath),
          img.width(), img.height());
    return ok ? 0 : 1;
  }

  QMainWindow win;
  win.setWindowTitle(QStringLiteral("farman 3D PoC"));
  QToolBar* tb = win.addToolBar(QStringLiteral("Tools"));
  QAction*  actTex = tb->addAction(QStringLiteral("テクスチャ"));
  actTex->setCheckable(true);
  actTex->setChecked(view->hasTexture() && !noTexture);
  actTex->setEnabled(view->hasTexture());
  actTex->setToolTip(QStringLiteral("テクスチャ表示の ON/OFF"));
  QObject::connect(actTex, &QAction::toggled, view, &Farman::ModelView::setTextureEnabled);

  win.setCentralWidget(view);
  win.statusBar()->showMessage(view->summary() + QStringLiteral("  |  ") + textureInfo(*view));
  win.resize(1000, 820);
  win.show();
  return app.exec();
}
