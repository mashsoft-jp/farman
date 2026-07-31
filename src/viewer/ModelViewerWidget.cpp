#include "viewer/ModelViewerWidget.h"

#include "viewer/ModelView.h"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QSizePolicy>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace Farman {

namespace {

enum class Glyph { Texture, Grid, Wire, Play, Pause, Reset, Help, Info };

void drawGlyph(QPainter& p, Glyph g, const QColor& c) {
  p.setRenderHint(QPainter::Antialiasing, true);
  QPen pen(c, 1.6);
  pen.setJoinStyle(Qt::RoundJoin);
  pen.setCapStyle(Qt::RoundCap);
  switch (g) {
    case Glyph::Texture: {  // 角丸 + チェッカー
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(QRectF(3, 3, 12, 12), 2, 2);
      p.setPen(Qt::NoPen);
      p.setBrush(c);
      p.drawRect(QRectF(3.5, 3.5, 5.5, 5.5));
      p.drawRect(QRectF(9, 9, 5.5, 5.5));
      break;
    }
    case Glyph::Grid: {
      p.setPen(pen);
      p.drawRect(QRectF(3, 3, 12, 12));
      p.drawLine(QPointF(7, 3), QPointF(7, 15));
      p.drawLine(QPointF(11, 3), QPointF(11, 15));
      p.drawLine(QPointF(3, 7), QPointF(15, 7));
      p.drawLine(QPointF(3, 11), QPointF(15, 11));
      break;
    }
    case Glyph::Wire: {  // 三角形をワイヤーフレーム (稜線 + 内部の分割線) で
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      const QPointF a(9, 3), b(3, 15), c(15, 15);
      p.drawLine(a, b);
      p.drawLine(b, c);
      p.drawLine(c, a);
      p.drawLine(a, QPointF(9, 15));  // 内部分割線でメッシュ感を出す
      break;
    }
    case Glyph::Play: {
      p.setPen(Qt::NoPen);
      p.setBrush(c);
      QPolygonF tri;
      tri << QPointF(6, 4) << QPointF(6, 14) << QPointF(14, 9);
      p.drawPolygon(tri);
      break;
    }
    case Glyph::Pause: {
      p.setPen(Qt::NoPen);
      p.setBrush(c);
      p.drawRoundedRect(QRectF(5, 4, 3, 10), 1, 1);
      p.drawRoundedRect(QRectF(10, 4, 3, 10), 1, 1);
      break;
    }
    case Glyph::Reset: {  // 円弧 + 矢じり (リロード風)
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      const QRectF r(4, 4, 10, 10);
      p.drawArc(r, 60 * 16, 280 * 16);
      // 矢じり (弧の始点付近)
      p.setPen(Qt::NoPen);
      p.setBrush(c);
      QPolygonF ah;
      ah << QPointF(11.5, 3.2) << QPointF(13.6, 6.2) << QPointF(10.0, 6.0);
      p.drawPolygon(ah);
      break;
    }
    case Glyph::Help: {  // 角丸 + 「?」
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(QRectF(3, 3, 12, 12), 3, 3);
      QFont f = p.font();
      f.setBold(true);
      f.setPixelSize(11);
      p.setFont(f);
      p.setPen(QPen(c));
      p.drawText(QRectF(3, 3, 12, 12), Qt::AlignCenter, QStringLiteral("?"));
      break;
    }
    case Glyph::Info: {
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawEllipse(QRectF(3, 3, 12, 12));
      p.setPen(Qt::NoPen);
      p.setBrush(c);
      p.drawEllipse(QPointF(9, 6.3), 1.1, 1.1);
      p.drawRoundedRect(QRectF(8.1, 8.2, 1.8, 5.0), 0.8, 0.8);
      break;
    }
  }
}

QIcon makeIcon(Glyph g, const QColor& c) {
  const qreal dpr = qApp ? qApp->devicePixelRatio() : 2.0;
  QPixmap     pm(QSize(18, 18) * dpr);
  pm.setDevicePixelRatio(dpr);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  drawGlyph(p, g, c);
  p.end();
  return QIcon(pm);
}

} // namespace

ModelViewerWidget::ModelViewerWidget(QWidget* parent) : QWidget(parent) {
  auto* lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(0);

  m_toolbar = new QToolBar(this);
  m_toolbar->setMovable(false);
  m_toolbar->setFloatable(false);
  m_toolbar->setIconSize(QSize(20, 20));
  m_view = new ModelView(this);

  lay->addWidget(m_toolbar);
  lay->addWidget(m_view, 1);
  setFocusProxy(m_view);

  const QColor ic = palette().color(QPalette::ButtonText);

  m_actTexture = m_toolbar->addAction(makeIcon(Glyph::Texture, ic), QString());
  m_actTexture->setCheckable(true);
  m_actTexture->setChecked(true);
  m_actTexture->setToolTip(QStringLiteral("テクスチャ表示 (T)"));
  connect(m_actTexture, &QAction::toggled, m_view, &ModelView::setTextureEnabled);
  connect(m_view, &ModelView::textureEnabledChanged, this, [this](bool on) {
    QSignalBlocker b(m_actTexture);
    m_actTexture->setChecked(on);
  });

  m_actGrid = m_toolbar->addAction(makeIcon(Glyph::Grid, ic), QString());
  m_actGrid->setCheckable(true);
  m_actGrid->setChecked(true);
  m_actGrid->setToolTip(QStringLiteral("床グリッド (G)"));
  connect(m_actGrid, &QAction::toggled, m_view, &ModelView::setShowGrid);
  connect(m_view, &ModelView::showGridChanged, this, [this](bool on) {
    QSignalBlocker b(m_actGrid);
    m_actGrid->setChecked(on);
  });

  m_actWire = m_toolbar->addAction(makeIcon(Glyph::Wire, ic), QString());
  m_actWire->setCheckable(true);
  m_actWire->setChecked(false);
  m_actWire->setToolTip(QStringLiteral("ワイヤーフレーム表示 (陰影なし) (F)"));
  connect(m_actWire, &QAction::toggled, m_view, &ModelView::setWireframe);
  connect(m_view, &ModelView::wireframeChanged, this, [this](bool on) {
    QSignalBlocker b(m_actWire);
    m_actWire->setChecked(on);
  });

  m_actHelp = m_toolbar->addAction(makeIcon(Glyph::Help, ic), QString());
  m_actHelp->setCheckable(true);
  m_actHelp->setChecked(true);
  m_actHelp->setToolTip(QStringLiteral("操作方法の表示 (H)"));
  connect(m_actHelp, &QAction::toggled, m_view, &ModelView::setShowHelp);
  connect(m_view, &ModelView::showHelpChanged, this, [this](bool on) {
    QSignalBlocker b(m_actHelp);
    m_actHelp->setChecked(on);
  });

  m_actPlay = m_toolbar->addAction(makeIcon(Glyph::Pause, ic), QString());
  m_actPlay->setCheckable(true);
  m_actPlay->setChecked(true);
  m_actPlay->setToolTip(QStringLiteral("アニメ再生 / 一時停止 (Space)"));
  connect(m_actPlay, &QAction::toggled, m_view, &ModelView::setAnimationPlaying);
  connect(m_actPlay, &QAction::toggled, this,
          [this, ic](bool on) { m_actPlay->setIcon(makeIcon(on ? Glyph::Pause : Glyph::Play, ic)); });
  connect(m_view, &ModelView::animationPlayingChanged, this, [this, ic](bool on) {
    QSignalBlocker b(m_actPlay);
    m_actPlay->setChecked(on);
    m_actPlay->setIcon(makeIcon(on ? Glyph::Pause : Glyph::Play, ic));
  });

  QAction* actReset = m_toolbar->addAction(makeIcon(Glyph::Reset, ic), QString());
  actReset->setToolTip(QStringLiteral("視点をリセット (R)"));
  connect(actReset, &QAction::triggered, m_view, &ModelView::resetView);

  m_toolbar->addSeparator();
  m_pathLabel = new QLabel(m_toolbar);
  m_pathLabel->setStyleSheet(QStringLiteral("color:#8a929c; padding:0 8px;"));
  m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_toolbar->addWidget(m_pathLabel);

  // 右端に情報ボタンを寄せるためのスペーサー。
  auto* spacer = new QWidget(m_toolbar);
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  m_toolbar->addWidget(spacer);

  // 情報ボタン: 画像ビュアーと同じデザイン (太字イタリックの「i」テキスト)。
  m_infoButton = new QToolButton(m_toolbar);
  m_infoButton->setText(QStringLiteral("i"));
  QFont infoFont = m_infoButton->font();
  infoFont.setItalic(true);
  infoFont.setBold(true);
  m_infoButton->setFont(infoFont);
  m_infoButton->setToolTip(QStringLiteral("モデル情報を別ウィンドウで表示 (i)"));
  m_infoButton->setFocusPolicy(Qt::NoFocus);
  connect(m_infoButton, &QToolButton::clicked, this, &ModelViewerWidget::openInfoDialog);
  m_toolbar->addWidget(m_infoButton);
  connect(m_view, &ModelView::infoRequested, this, &ModelViewerWidget::openInfoDialog);
}

void ModelViewerWidget::openInfoDialog() {
  if (!m_infoDialog) {
    m_infoDialog = new QDialog(this);
    m_infoDialog->setWindowTitle(QStringLiteral("モデル情報"));
    auto* dl = new QVBoxLayout(m_infoDialog);
    m_infoText = new QPlainTextEdit(m_infoDialog);
    m_infoText->setReadOnly(true);
    dl->addWidget(m_infoText);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, m_infoDialog);
    connect(bb, &QDialogButtonBox::rejected, m_infoDialog, &QDialog::close);
    dl->addWidget(bb);
    m_infoDialog->resize(480, 260);
  }
  m_infoText->setPlainText(m_view->infoLines().join(QStringLiteral("\n")));
  m_infoDialog->show();
  m_infoDialog->raise();
  m_infoDialog->activateWindow();
}

void ModelViewerWidget::refreshToolbar() {
  const bool hasTex = m_view->hasTexture();
  m_actTexture->setEnabled(hasTex);
  {
    QSignalBlocker b(m_actTexture);
    m_actTexture->setChecked(hasTex);
  }
  m_actPlay->setVisible(m_view->hasAnimation());

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
  if (m_infoDialog && m_infoDialog->isVisible())
    m_infoText->setPlainText(m_view->infoLines().join(QStringLiteral("\n")));
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
