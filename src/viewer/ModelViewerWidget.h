#pragma once

// 3D モデルビュアーの外枠。上部にツールバー (テクスチャ ON/OFF・グリッド・情報・
// 再生・リセット + 外部テクスチャのパス表示) を持ち、下に ModelView (描画) を置く。
// farman の他ビュアーと同様に「ビュアー上部のツールバー」で操作できるようにする。
// プラグイン (ModelViewerPlugin::createViewer) はこのウィジェットを返す。

#include <QImage>
#include <QString>
#include <QStringList>
#include <QWidget>

class QToolBar;
class QAction;
class QToolButton;
class QLabel;
class QDialog;
class QPlainTextEdit;

namespace Farman {

class ModelView;

class ModelViewerWidget : public QWidget {
  Q_OBJECT

public:
  explicit ModelViewerWidget(QWidget* parent = nullptr);

  bool loadModel(const QString& path, QString* error = nullptr);

  // ── 委譲 (PoC / ホスト用) ──
  QImage      renderToImage();
  QString     summary() const;
  bool        hasAnimation() const;
  double      animationDuration() const;
  bool        hasTexture() const;
  bool        hasEmbeddedTexture() const;
  bool        hasUV() const;
  QStringList recordedTexturePaths() const;
  QStringList resolvedTexturePaths() const;
  QStringList unresolvedTexturePaths() const;
  void        setTextureEnabled(bool on);
  void        setAnimationTime(double sec);

  ModelView* view() const { return m_view; }

private:
  void refreshToolbar();
  void openInfoDialog();

  ModelView*      m_view       = nullptr;
  QToolBar*       m_toolbar    = nullptr;
  QAction*        m_actTexture = nullptr;
  QAction*        m_actGrid    = nullptr;
  QAction*        m_actWire    = nullptr;
  QAction*        m_actBones   = nullptr;
  QAction*        m_actHelp    = nullptr;
  QAction*        m_actPlay    = nullptr;
  QToolButton*    m_infoButton = nullptr;
  QLabel*         m_pathLabel  = nullptr;
  QDialog*        m_infoDialog = nullptr;
  QPlainTextEdit* m_infoText   = nullptr;
};

} // namespace Farman
