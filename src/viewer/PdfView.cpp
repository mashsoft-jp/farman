#include "PdfView.h"

#include "utils/EnterClickFilter.h"

#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace Farman {

namespace {
constexpr qreal kZoomStep    = 1.25;   // 25% ずつ
constexpr qreal kZoomMin     = 0.10;
constexpr qreal kZoomMax     = 8.00;
constexpr qreal kDefaultZoom = 1.00;
} // namespace

PdfView::PdfView(QWidget* parent) : QWidget(parent) {
  setupUi();
}

PdfView::~PdfView() = default;

void PdfView::setupUi() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  m_toolbar = new QToolBar(this);
  m_toolbar->setMovable(false);
  m_toolbar->setFloatable(false);
  m_toolbar->setIconSize(QSize(20, 20));
  m_toolbar->setStyleSheet(toolbarStyleSheet());

  // ───── ページ操作 ─────
  m_prevButton = new QToolButton(m_toolbar);
  m_prevButton->setText(QStringLiteral("◀"));
  m_prevButton->setToolTip(tr("Previous page (PageUp)"));
  m_prevButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_prevButton, &QToolButton::clicked, this, &PdfView::onPrevPage);
  m_toolbar->addWidget(m_prevButton);

  m_pageSpin = new QSpinBox(m_toolbar);
  m_pageSpin->setRange(1, 1);
  m_pageSpin->setValue(1);
  m_pageSpin->setFixedWidth(60);
  m_pageSpin->setAlignment(Qt::AlignRight);
  m_pageSpin->setToolTip(tr("Jump to page"));
  // editingFinished だと値を入れただけでは飛ばないので valueChanged を使う。
  // ただし無限ループ防止のため、onCurrentPageChanged 側で blockSignals する。
  connect(m_pageSpin, qOverload<int>(&QSpinBox::valueChanged),
          this,        &PdfView::onPageSpinChanged);
  m_toolbar->addWidget(m_pageSpin);

  m_pageTotalLabel = new QLabel(QStringLiteral("/ 1"), m_toolbar);
  m_pageTotalLabel->setContentsMargins(2, 0, 6, 0);
  m_toolbar->addWidget(m_pageTotalLabel);

  m_nextButton = new QToolButton(m_toolbar);
  m_nextButton->setText(QStringLiteral("▶"));
  m_nextButton->setToolTip(tr("Next page (PageDown)"));
  m_nextButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_nextButton, &QToolButton::clicked, this, &PdfView::onNextPage);
  m_toolbar->addWidget(m_nextButton);

  m_toolbar->addSeparator();

  // ───── ズーム ─────
  m_zoomOutButton = new QToolButton(m_toolbar);
  m_zoomOutButton->setText(QStringLiteral("−"));
  m_zoomOutButton->setToolTip(tr("Zoom out (Ctrl+-)"));
  m_zoomOutButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_zoomOutButton, &QToolButton::clicked, this, &PdfView::onZoomOut);
  m_toolbar->addWidget(m_zoomOutButton);

  m_zoomInButton = new QToolButton(m_toolbar);
  m_zoomInButton->setText(QStringLiteral("+"));
  m_zoomInButton->setToolTip(tr("Zoom in (Ctrl++)"));
  m_zoomInButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_zoomInButton, &QToolButton::clicked, this, &PdfView::onZoomIn);
  m_toolbar->addWidget(m_zoomInButton);

  m_fitWidthButton = new QToolButton(m_toolbar);
  m_fitWidthButton->setText(tr("Fit Width"));
  m_fitWidthButton->setCheckable(true);
  m_fitWidthButton->setToolTip(tr("Fit page width to view"));
  m_fitWidthButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_fitWidthButton, &QToolButton::toggled, this, &PdfView::onFitWidth);
  m_toolbar->addWidget(m_fitWidthButton);

  m_fitPageButton = new QToolButton(m_toolbar);
  m_fitPageButton->setText(tr("Fit Page"));
  m_fitPageButton->setCheckable(true);
  m_fitPageButton->setToolTip(tr("Fit whole page in view"));
  m_fitPageButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_fitPageButton, &QToolButton::toggled, this, &PdfView::onFitPage);
  m_toolbar->addWidget(m_fitPageButton);

  m_toolbar->addSeparator();

  // ───── モード ─────
  m_continuousButton = new QToolButton(m_toolbar);
  m_continuousButton->setText(tr("Continuous"));
  m_continuousButton->setCheckable(true);
  m_continuousButton->setChecked(true);
  m_continuousButton->setToolTip(
    tr("Continuous multi-page scrolling (off = single page)"));
  m_continuousButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_continuousButton, &QToolButton::toggled,
          this,                &PdfView::onPageModeToggled);
  m_toolbar->addWidget(m_continuousButton);

  auto* clickFilter = new EnterClickFilter(this);
  clickFilter->installOnButtonsIn(m_toolbar);

  root->addWidget(m_toolbar);

  // ───── 本体: QPdfView ─────
  m_view = new QPdfView(this);
  m_view->setPageMode(QPdfView::PageMode::MultiPage);
  m_view->setZoomMode(QPdfView::ZoomMode::Custom);
  m_view->setZoomFactor(kDefaultZoom);
  root->addWidget(m_view, /*stretch*/ 1);

  setFocusProxy(m_view);
}

bool PdfView::loadFile(const QString& filePath) {
  PreparedLoad p = prepareLoad(filePath);
  if (!p.ok) return false;
  applyPreparedLoad(p);
  return true;
}

PdfView::PreparedLoad PdfView::prepareLoad(const QString& filePath,
                                            const std::atomic<bool>* cancelToken) {
  PreparedLoad r;
  r.filePath = filePath;

  if (cancelToken && cancelToken->load(std::memory_order_acquire)) return r;

  const QFileInfo fi(filePath);
  if (!fi.exists() || !fi.isFile() || !fi.isReadable()) return r;

  r.fileSize = fi.size();
  r.ok       = true;
  return r;
}

void PdfView::applyPreparedLoad(const PreparedLoad& r) {
  m_filePath = r.filePath;
  m_fileSize = r.fileSize;

  // 既存のドキュメントは破棄してから新規生成。QPdfView は document の所有権を
  // 取らないので、こちら側で明示的に管理する必要がある。
  if (m_document) {
    m_view->setDocument(nullptr);
    delete m_document;
    m_document = nullptr;
  }
  m_document = new QPdfDocument(this);

  // pageCount が確定したらツールバーの spinbox を更新。
  connect(m_document, &QPdfDocument::pageCountChanged,
          this,        &PdfView::onPageCountChanged);

  const QPdfDocument::Error err = m_document->load(m_filePath);
  if (err != QPdfDocument::Error::None) {
    delete m_document;
    m_document = nullptr;
    return;
  }

  m_view->setDocument(m_document);

  // pageNavigator は QPdfView 内部で生成されている。currentPageChanged を
  // ツールバーの spinbox に反映する。
  if (auto* nav = m_view->pageNavigator()) {
    connect(nav, &QPdfPageNavigator::currentPageChanged,
            this, &PdfView::onCurrentPageChanged, Qt::UniqueConnection);
  }

  onPageCountChanged(m_document->pageCount());
  updatePageLabel();
}

void PdfView::clearContent() {
  if (m_view && m_document) {
    m_view->setDocument(nullptr);
  }
  if (m_document) {
    delete m_document;
    m_document = nullptr;
  }
  m_filePath.clear();
  m_fileSize = 0;
  if (m_pageSpin) {
    m_pageSpin->blockSignals(true);
    m_pageSpin->setRange(1, 1);
    m_pageSpin->setValue(1);
    m_pageSpin->blockSignals(false);
  }
  if (m_pageTotalLabel) m_pageTotalLabel->setText(QStringLiteral("/ 1"));
}

QString PdfView::statusInfo() const {
  if (m_filePath.isEmpty() || !m_document) return QString();
  const int pages = m_document->pageCount();
  return tr("PDF · %1 page(s) · %2", "PDF status: page count + file size",
            pages)
           .arg(QLocale().toString(pages))
           .arg(QLocale(QLocale::English).formattedDataSize(m_fileSize));
}

void PdfView::onPrevPage() {
  if (!m_view || !m_document) return;
  auto* nav = m_view->pageNavigator();
  if (!nav) return;
  const int p = nav->currentPage();
  if (p > 0) nav->jump(p - 1, QPointF(), nav->currentZoom());
}

void PdfView::onNextPage() {
  if (!m_view || !m_document) return;
  auto* nav = m_view->pageNavigator();
  if (!nav) return;
  const int p = nav->currentPage();
  if (p + 1 < m_document->pageCount()) {
    nav->jump(p + 1, QPointF(), nav->currentZoom());
  }
}

void PdfView::onPageSpinChanged(int displayPage) {
  // 1-based UI から 0-based 内部に変換。
  if (!m_view || !m_document) return;
  auto* nav = m_view->pageNavigator();
  if (!nav) return;
  const int target = displayPage - 1;
  if (target == nav->currentPage()) return;
  nav->jump(target, QPointF(), nav->currentZoom());
}

void PdfView::onZoomIn() {
  if (!m_view) return;
  // FitWidth / FitPage が ON なら Custom に戻してから倍率変更。
  m_fitWidthButton->setChecked(false);
  m_fitPageButton->setChecked(false);
  m_view->setZoomMode(QPdfView::ZoomMode::Custom);
  qreal z = m_view->zoomFactor() * kZoomStep;
  if (z > kZoomMax) z = kZoomMax;
  m_view->setZoomFactor(z);
}

void PdfView::onZoomOut() {
  if (!m_view) return;
  m_fitWidthButton->setChecked(false);
  m_fitPageButton->setChecked(false);
  m_view->setZoomMode(QPdfView::ZoomMode::Custom);
  qreal z = m_view->zoomFactor() / kZoomStep;
  if (z < kZoomMin) z = kZoomMin;
  m_view->setZoomFactor(z);
}

void PdfView::onFitWidth(bool checked) {
  if (!m_view) return;
  if (checked) {
    // 片方しか ON にしない
    m_fitPageButton->blockSignals(true);
    m_fitPageButton->setChecked(false);
    m_fitPageButton->blockSignals(false);
    m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
  } else if (!m_fitPageButton->isChecked()) {
    m_view->setZoomMode(QPdfView::ZoomMode::Custom);
  }
}

void PdfView::onFitPage(bool checked) {
  if (!m_view) return;
  if (checked) {
    m_fitWidthButton->blockSignals(true);
    m_fitWidthButton->setChecked(false);
    m_fitWidthButton->blockSignals(false);
    m_view->setZoomMode(QPdfView::ZoomMode::FitInView);
  } else if (!m_fitWidthButton->isChecked()) {
    m_view->setZoomMode(QPdfView::ZoomMode::Custom);
  }
}

void PdfView::onPageModeToggled(bool continuous) {
  if (!m_view) return;
  m_view->setPageMode(continuous ? QPdfView::PageMode::MultiPage
                                  : QPdfView::PageMode::SinglePage);
}

void PdfView::onCurrentPageChanged(int page) {
  // 0-based → spinbox は 1-based。
  if (!m_pageSpin) return;
  m_pageSpin->blockSignals(true);
  m_pageSpin->setValue(page + 1);
  m_pageSpin->blockSignals(false);
}

void PdfView::onPageCountChanged(int pageCount) {
  if (!m_pageSpin || !m_pageTotalLabel) return;
  const int safeMax = pageCount > 0 ? pageCount : 1;
  m_pageSpin->blockSignals(true);
  m_pageSpin->setRange(1, safeMax);
  m_pageSpin->blockSignals(false);
  m_pageTotalLabel->setText(QStringLiteral("/ %1").arg(safeMax));
}

void PdfView::updatePageLabel() {
  if (!m_view || !m_document || !m_pageSpin) return;
  auto* nav = m_view->pageNavigator();
  if (!nav) return;
  m_pageSpin->blockSignals(true);
  m_pageSpin->setValue(nav->currentPage() + 1);
  m_pageSpin->blockSignals(false);
}

} // namespace Farman
