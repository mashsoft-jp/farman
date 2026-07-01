#include "PdfView.h"

#include "settings/Settings.h"
#include "utils/EnterClickFilter.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPdfDocument>
#include <QPdfLink>
#include <QPdfPageNavigator>
#include <QPdfSearchModel>
#include <QPdfView>
#include <QSizePolicy>
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

// ページ番号スピンボックス。一般的な PDF ビューアと同じく
// 「↑ = 前ページ・↓ = 次ページ」になるよう、矢印キーだけ反転する。
// QSpinBox 既定では ↑ で値 +1 (= ページ番号が増えて次ページ)・↓ で -1 と
// なり PDF のページ送りとしては直感と逆なので、keyPressEvent を override して
// 向きを入れ替える (▲▼ ボタンは標準動作のまま)。
//
// 注意: macOS では矢印キーに Qt::KeypadModifier が付く。NoModifier と厳密
// 比較すると一致せず判定をすり抜けるため、実モディファイア (Shift / Ctrl /
// Alt / Meta) だけを見る (Transfer/Archive 各ダイアログと同じ作法)。
class PageSpinBox : public QSpinBox {
public:
  using QSpinBox::QSpinBox;

protected:
  void keyPressEvent(QKeyEvent* event) override {
    const auto mods = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier
                                            | Qt::AltModifier | Qt::MetaModifier);
    if (mods == Qt::NoModifier
        && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
      stepBy(event->key() == Qt::Key_Up ? -1 : +1);
      event->accept();
      return;
    }
    QSpinBox::keyPressEvent(event);
  }
};
} // namespace

PdfView::PdfView(QWidget* parent) : QWidget(parent) {
  setupUi();
}

PdfView::~PdfView() {
  // 破棄時、子として持つ QPdfDocument が deleteChildren で close() され、
  // pageCountChanged(0) を emit する。このとき既に破棄された m_pageSpin へ
  // onPageCountChanged がアクセスしてクラッシュする (外部ウィンドウを Enter で
  // 閉じたとき等)。先にドキュメントからの signal を切っておく。
  if (m_document) {
    m_document->disconnect(this);
  }
}

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

  m_pageSpin = new PageSpinBox(m_toolbar);
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

  m_toolbar->addSeparator();

  // ───── 検索 (TextView と同じレイアウト: Find: [入力] [前] [次] [件数]) ─────
  m_toolbar->addWidget(new QLabel(tr("Find:"), m_toolbar));

  const QString findShortcutText =
    QKeySequence(QKeySequence::Find).toString(QKeySequence::NativeText);

  m_findEdit = new QLineEdit(m_toolbar);
  m_findEdit->setPlaceholderText(tr("Search text  (%1)").arg(findShortcutText));
  m_findEdit->setToolTip(tr("Search text in this PDF (%1)").arg(findShortcutText));
  m_findEdit->setClearButtonEnabled(true);
  m_findEdit->setFocusPolicy(Qt::StrongFocus);
  m_findEdit->setMinimumWidth(160);
  m_findEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_findEdit->installEventFilter(this);
  m_toolbar->addWidget(m_findEdit);

  m_findPrevBtn = new QToolButton(m_toolbar);
  m_findPrevBtn->setText(QStringLiteral("◀"));
  m_findPrevBtn->setToolTip(tr("Previous match (Shift+Enter)"));
  m_findPrevBtn->setFocusPolicy(Qt::StrongFocus);
  connect(m_findPrevBtn, &QToolButton::clicked, this, &PdfView::findPrevious);
  m_toolbar->addWidget(m_findPrevBtn);

  m_findNextBtn = new QToolButton(m_toolbar);
  m_findNextBtn->setText(QStringLiteral("▶"));
  m_findNextBtn->setToolTip(tr("Next match (Enter)"));
  m_findNextBtn->setFocusPolicy(Qt::StrongFocus);
  connect(m_findNextBtn, &QToolButton::clicked, this, &PdfView::findNext);
  m_toolbar->addWidget(m_findNextBtn);

  m_findStatus = new QLabel(m_toolbar);
  m_findStatus->setMinimumWidth(80);
  m_toolbar->addWidget(m_findStatus);

  connect(m_findEdit, &QLineEdit::textChanged,
          this,        &PdfView::onFindTextChanged);

  // QPdfSearchModel は document をセットすると searchString に応じて
  // 全件マッチを返してくれる。ビューに setSearchModel するとハイライト描画。
  m_searchModel = new QPdfSearchModel(this);
  connect(m_searchModel, &QPdfSearchModel::countChanged,
          this,           &PdfView::onFindCountChanged);

  // Cmd/Ctrl+F でフォーカスを検索欄に飛ばす (TextView と同じ作法)
  qApp->installEventFilter(this);

  auto* clickFilter = new EnterClickFilter(this);
  clickFilter->installOnButtonsIn(m_toolbar);

  root->addWidget(m_toolbar);

  // ───── 本体: QPdfView ─────
  m_view = new QPdfView(this);
  m_view->setPageMode(QPdfView::PageMode::MultiPage);
  m_view->setZoomMode(QPdfView::ZoomMode::Custom);
  m_view->setZoomFactor(kDefaultZoom);
  root->addWidget(m_view, /*stretch*/ 1);

  // 既定の表示設定 (Settings → Plugins → PDF Viewer)。トグルの signal は
  // ドキュメント未ロードでも m_view に対して安全に効くので、ここで適用する。
  {
    Settings& s = Settings::instance();
    m_continuousButton->setChecked(s.pdfViewerContinuous());
    switch (s.pdfViewerFitMode()) {
      case PdfViewerFitMode::FitWidth: m_fitWidthButton->setChecked(true); break;
      case PdfViewerFitMode::FitPage:  m_fitPageButton->setChecked(true);  break;
      case PdfViewerFitMode::ActualSize:
      default: break;
    }
  }

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

  // 検索モデルを新しい document に再バインド。ファイル切替で前の文書の
  // 検索状態が残らないよう、searchString も一旦クリアして UI も初期化する。
  if (m_searchModel) {
    m_searchModel->setSearchString(QString());
    m_searchModel->setDocument(m_document);
    m_view->setSearchModel(m_searchModel);
  }
  m_currentSearchIndex = -1;
  if (m_findEdit)   m_findEdit->clear();
  if (m_findStatus) m_findStatus->clear();

  onPageCountChanged(m_document->pageCount());
  updatePageLabel();
}

void PdfView::clearContent() {
  // 検索モデルを先に切り離す。document を破棄してから残っているとモデルが
  // 不正な document* に触れてしまう。
  if (m_searchModel) {
    m_searchModel->setSearchString(QString());
    m_searchModel->setDocument(nullptr);
  }
  m_currentSearchIndex = -1;
  if (m_findEdit)   m_findEdit->clear();
  if (m_findStatus) m_findStatus->clear();

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

// ---------- 検索 ----------

void PdfView::onFindTextChanged(const QString& text) {
  // テキストが変わったら全件再検索 (QPdfSearchModel が非同期で count を更新)
  if (!m_searchModel) return;
  m_searchModel->setSearchString(text);
  m_currentSearchIndex = -1;
  if (text.isEmpty()) {
    if (m_findStatus) m_findStatus->clear();
  } else {
    // count はまだ確定していないかも。countChanged 経由でも updateFindStatus が
    // 呼ばれるので、ここでも一度仮表示を出す。
    updateFindStatus();
  }
}

void PdfView::onFindCountChanged() {
  // モデルが検索完了したタイミング。ステータス表示を更新。
  updateFindStatus();
}

void PdfView::findNext() {
  if (!m_searchModel || !m_findEdit) return;
  const int total = m_searchModel->count();
  if (total <= 0) {
    updateFindStatus();
    return;
  }
  m_currentSearchIndex = (m_currentSearchIndex + 1) % total;
  jumpToSearchResult(m_currentSearchIndex);
}

void PdfView::findPrevious() {
  if (!m_searchModel || !m_findEdit) return;
  const int total = m_searchModel->count();
  if (total <= 0) {
    updateFindStatus();
    return;
  }
  m_currentSearchIndex =
    (m_currentSearchIndex <= 0) ? (total - 1) : (m_currentSearchIndex - 1);
  jumpToSearchResult(m_currentSearchIndex);
}

void PdfView::jumpToSearchResult(int index) {
  if (!m_searchModel || !m_view) return;
  // ビューに現在ヒット index を伝えると、そのヒットに赤丸風の強調が乗る。
  m_view->setCurrentSearchResultIndex(index);
  // ページにスクロールするのは pageNavigator 経由。QPdfLink から page と
  // location を取って jump する。
  const QPdfLink link = m_searchModel->resultAtIndex(index);
  if (auto* nav = m_view->pageNavigator()) {
    nav->jump(link.page(), link.location(), nav->currentZoom());
  }
  updateFindStatus();
}

void PdfView::updateFindStatus() {
  if (!m_findStatus || !m_searchModel || !m_findEdit) return;
  if (m_findEdit->text().isEmpty()) {
    m_findStatus->clear();
    return;
  }
  const int total = m_searchModel->count();
  if (total <= 0) {
    m_findStatus->setText(tr("Not found"));
  } else if (m_currentSearchIndex < 0) {
    m_findStatus->setText(tr("%1 matches").arg(total));
  } else {
    m_findStatus->setText(tr("%1 / %2")
                             .arg(m_currentSearchIndex + 1)
                             .arg(total));
  }
}

void PdfView::focusFindInput() {
  if (!m_findEdit) return;
  m_findEdit->setFocus(Qt::ShortcutFocusReason);
  m_findEdit->selectAll();
}

bool PdfView::eventFilter(QObject* watched, QEvent* event) {
  // 検索入力欄の Enter / Shift+Enter / Esc
  if (watched == m_findEdit && event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() == Qt::Key_Escape) {
      if (m_view) m_view->setFocus(Qt::OtherFocusReason);
      return true;
    }
    if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
      if (ke->modifiers() & Qt::ShiftModifier) findPrevious();
      else                                     findNext();
      return true;
    }
  }

  // Cmd/Ctrl+F: ビューが表示されているときだけ拾って検索欄にフォーカスを移す。
  // TextView と同じく ShortcutOverride + KeyPress 両方で捕捉して上位の
  // ショートカット (メニュー等) に取られるのを防ぐ。
  if (event->type() == QEvent::ShortcutOverride
      || event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    const bool isFindKey =
      ke->key() == Qt::Key_F &&
      (ke->modifiers() & Qt::ControlModifier);  // macOS では Cmd
    if (isFindKey && isVisible() && window() && window()->isActiveWindow()) {
      if (event->type() == QEvent::KeyPress) focusFindInput();
      event->accept();
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

} // namespace Farman
