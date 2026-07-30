// farman 3D モデルビュアー PoC。
// ModelViewerWidget (ツールバー + ModelView) を表示する検証用アプリ。
//   farman_model_poc <model file>                       … ウィンドウ表示
//   farman_model_poc <model file> --shot <png>          … 3D をオフスクリーン PNG
//   farman_model_poc <model file> --shot <png> --time <s> … 指定アニメ時刻で
//   farman_model_poc <model file> --uishot <png>        … UI 込みの見た目を PNG
//   ... --no-texture                                    … テクスチャ OFF

#include "viewer/ModelViewerWidget.h"

#include <QApplication>
#include <QImage>
#include <QPixmap>
#include <QStringList>
#include <QSurfaceFormat>

namespace {

QString textureInfo(const Farman::ModelViewerWidget& v) {
  if (!v.hasTexture()) return QStringLiteral("テクスチャ: なし");
  QStringList parts;
  if (v.hasEmbeddedTexture()) parts << QStringLiteral("埋め込み");
  parts << v.resolvedTexturePaths();
  for (const QString& p : v.unresolvedTexturePaths()) parts << (p + QStringLiteral(" (見つからず)"));
  QString s = QStringLiteral("テクスチャ: ") + parts.join(QStringLiteral(" / "));
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
  QSurfaceFormat::setDefaultFormat(fmt);

  QString modelPath, shotPath, uishotPath;
  bool    noTexture = false;
  double  timeOpt   = -1.0;
  const QStringList args = app.arguments();
  for (int i = 1; i < args.size(); ++i) {
    if (args[i] == QLatin1String("--shot") && i + 1 < args.size())
      shotPath = args[++i];
    else if (args[i] == QLatin1String("--uishot") && i + 1 < args.size())
      uishotPath = args[++i];
    else if (args[i] == QLatin1String("--time") && i + 1 < args.size())
      timeOpt = args[++i].toDouble();
    else if (args[i] == QLatin1String("--no-texture"))
      noTexture = true;
    else if (modelPath.isEmpty())
      modelPath = args[i];
  }
  if (modelPath.isEmpty()) {
    qWarning("usage: farman_model_poc <model file> [--shot <png>] [--uishot <png>] "
             "[--time <sec>] [--no-texture]");
    return 2;
  }

  auto* view = new Farman::ModelViewerWidget;
  view->resize(1000, 800);

  QString err;
  if (!view->loadModel(modelPath, &err)) {
    qWarning("load failed: %s", qPrintable(err));
    return 1;
  }
  if (noTexture) view->setTextureEnabled(false);

  qInfo("loaded: %s", qPrintable(view->summary()));
  qInfo("%s", qPrintable(textureInfo(*view)));
  for (const QString& p : view->recordedTexturePaths())
    qInfo("  外部テクスチャ記録パス: %s", qPrintable(p));
  for (const QString& p : view->resolvedTexturePaths())
    qInfo("  解決パス              : %s", qPrintable(p));
  if (view->hasAnimation()) qInfo("animation: %.2f s", view->animationDuration());

  if (!uishotPath.isEmpty()) {
    view->renderToImage();  // 3D フレームを用意
    const QPixmap pm = view->grab();
    const bool    ok = pm.save(uishotPath);
    qInfo("uishot %s: %s", ok ? "saved" : "FAILED", qPrintable(uishotPath));
    return ok ? 0 : 1;
  }
  if (!shotPath.isEmpty()) {
    if (view->hasAnimation()) {
      const double t = timeOpt >= 0 ? timeOpt : view->animationDuration() * 0.5;
      view->setAnimationTime(t);
    }
    const QImage img = view->renderToImage();
    const bool   ok  = img.save(shotPath);
    qInfo("shot %s: %s (%dx%d)", ok ? "saved" : "FAILED", qPrintable(shotPath), img.width(),
          img.height());
    return ok ? 0 : 1;
  }

  view->setWindowTitle(QStringLiteral("farman 3D PoC — ") + view->summary());
  view->show();
  return app.exec();
}
