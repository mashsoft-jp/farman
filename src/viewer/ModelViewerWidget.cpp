#include "viewer/ModelViewerWidget.h"

#include "viewer/ModelView.h"

#include <QAction>
#include <QLabel>
#include <QToolBar>
#include <QVBoxLayout>

namespace Farman {

ModelViewerWidget::ModelViewerWidget(QWidget* parent) : QWidget(parent) {
  auto* lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(0);

  m_toolbar = new QToolBar(this);
  m_toolbar->setMovable(false);
  m_toolbar->setFloatable(false);
  m_view = new ModelView(this);

  lay->addWidget(m_toolbar);
  lay->addWidget(m_view, 1);
  setFocusProxy(m_view);

  auto sync = [this](QAction* a, bool on) {
    QSignalBlocker b(a);
    a->setChecked(on);
  };

  m_actTexture = m_toolbar->addAction(QStringLiteral("テクスチャ"));
  m_actTexture->setCheckable(true);
  m_actTexture->setChecked(true);
  m_actTexture->setToolTip(QStringLiteral("テクスチャ表示の ON/OFF (T)"));
  connect(m_actTexture, &QAction::toggled, m_view, &ModelView::setTextureEnabled);
  connect(m_view, &ModelView::textureEnabledChanged, this,
          [this, sync](bool on) { sync(m_actTexture, on); });

  m_actGrid = m_toolbar->addAction(QStringLiteral("グリッド"));
  m_actGrid->setCheckable(true);
  m_actGrid->setChecked(true);
  m_actGrid->setToolTip(QStringLiteral("床グリッド (G)"));
  connect(m_actGrid, &QAction::toggled, m_view, &ModelView::setShowGrid);
  connect(m_view, &ModelView::showGridChanged, this,
          [this, sync](bool on) { sync(m_actGrid, on); });

  m_actInfo = m_toolbar->addAction(QStringLiteral("情報"));
  m_actInfo->setCheckable(true);
  m_actInfo->setToolTip(QStringLiteral("モデル情報の表示 (i)"));
  connect(m_actInfo, &QAction::toggled, m_view, &ModelView::setShowInfo);
  connect(m_view, &ModelView::showInfoChanged, this,
          [this, sync](bool on) { sync(m_actInfo, on); });

  m_actPlay = m_toolbar->addAction(QStringLiteral("再生"));
  m_actPlay->setCheckable(true);
  m_actPlay->setChecked(true);
  m_actPlay->setToolTip(QStringLiteral("アニメ再生 / 一時停止 (Space)"));
  connect(m_actPlay, &QAction::toggled, m_view, &ModelView::setAnimationPlaying);
  connect(m_view, &ModelView::animationPlayingChanged, this,
          [this, sync](bool on) { sync(m_actPlay, on); });

  QAction* actReset = m_toolbar->addAction(QStringLiteral("リセット"));
  actReset->setToolTip(QStringLiteral("視点をリセット (R)"));
  connect(actReset, &QAction::triggered, m_view, &ModelView::resetView);

  m_toolbar->addSeparator();
  m_pathLabel = new QLabel(m_toolbar);
  m_pathLabel->setStyleSheet(QStringLiteral("color:#8a929c; padding:0 8px;"));
  m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_toolbar->addWidget(m_pathLabel);
}

void ModelViewerWidget::refreshToolbar() {
  const bool hasTex = m_view->hasTexture();
  m_actTexture->setEnabled(hasTex);
  {
    QSignalBlocker b(m_actTexture);
    m_actTexture->setChecked(hasTex);
  }
  m_actPlay->setVisible(m_view->hasAnimation());

  // 外部テクスチャのパスをツールバーに表示 (外部参照時のみ)。埋め込みは種別のみ。
  const QStringList ext = m_view->resolvedTexturePaths() + m_view->unresolvedTexturePaths();
  if (!ext.isEmpty()) {
    m_pathLabel->setText(QStringLiteral("テクスチャ: ") + ext.join(QStringLiteral("  /  ")));
    m_pathLabel->show();
  } else if (m_view->hasEmbeddedTexture()) {
    m_pathLabel->setText(QStringLiteral("テクスチャ: 埋め込み"));
    m_pathLabel->show();
  } else {
    m_pathLabel->clear();
    m_pathLabel->hide();
  }
}

bool ModelViewerWidget::loadModel(const QString& path, QString* error) {
  const bool ok = m_view->loadModel(path, error);
  refreshToolbar();
  return ok;
}

QImage      ModelViewerWidget::renderToImage() { return m_view->renderToImage(); }
QString     ModelViewerWidget::summary() const { return m_view->summary(); }
bool        ModelViewerWidget::hasAnimation() const { return m_view->hasAnimation(); }
double      ModelViewerWidget::animationDuration() const { return m_view->animationDuration(); }
bool        ModelViewerWidget::hasTexture() const { return m_view->hasTexture(); }
bool        ModelViewerWidget::hasEmbeddedTexture() const { return m_view->hasEmbeddedTexture(); }
bool        ModelViewerWidget::hasUV() const { return m_view->hasUV(); }
QStringList ModelViewerWidget::recordedTexturePaths() const { return m_view->recordedTexturePaths(); }
QStringList ModelViewerWidget::resolvedTexturePaths() const { return m_view->resolvedTexturePaths(); }
QStringList ModelViewerWidget::unresolvedTexturePaths() const {
  return m_view->unresolvedTexturePaths();
}
void ModelViewerWidget::setTextureEnabled(bool on) { m_view->setTextureEnabled(on); }
void ModelViewerWidget::setAnimationTime(double sec) { m_view->setAnimationTime(sec); }

} // namespace Farman
