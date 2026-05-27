#include "PreviewPane.h"

#include <QApplication>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

namespace Farman {

PreviewPane::PreviewPane(QWidget* parent) : QWidget(parent) {
  setupUi();
}

void PreviewPane::setupUi() {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_stack = new QStackedWidget(this);
  layout->addWidget(m_stack);

  // 共通の "状態表示" 用ラベルスタイル。
  const QString stateStyle = QStringLiteral(
    "QLabel { color: palette(mid); padding: 16px; }");

  // 0: Empty
  m_emptyLabel = new QLabel(tr("(No selection)"), this);
  m_emptyLabel->setAlignment(Qt::AlignCenter);
  m_emptyLabel->setStyleSheet(stateStyle);
  m_emptyLabel->setWordWrap(true);
  m_stack->addWidget(m_emptyLabel);

  // 1: Loading
  m_loadingLabel = new QLabel(tr("Loading..."), this);
  m_loadingLabel->setAlignment(Qt::AlignCenter);
  m_loadingLabel->setStyleSheet(stateStyle);
  m_loadingLabel->setWordWrap(true);
  m_stack->addWidget(m_loadingLabel);

  // 2: Unsupported / status (テキストは動的に差し替え)
  m_unsupportedLabel = new QLabel(QString(), this);
  m_unsupportedLabel->setAlignment(Qt::AlignCenter);
  m_unsupportedLabel->setStyleSheet(stateStyle);
  m_unsupportedLabel->setWordWrap(true);
  m_stack->addWidget(m_unsupportedLabel);

  // 3: Directory page (アイコン + パス + 件数 を縦に並べる Finder Quick Look 風)
  m_directoryPage = new QWidget(this);
  auto* dirLayout = new QVBoxLayout(m_directoryPage);
  dirLayout->setContentsMargins(16, 32, 16, 32);
  dirLayout->setSpacing(12);
  dirLayout->addStretch();

  m_directoryIcon = new QLabel(m_directoryPage);
  m_directoryIcon->setAlignment(Qt::AlignCenter);
  // 大きめのフォルダアイコンを描画。OS 標準のフォルダアイコンを 96px で取得。
  const QIcon folderIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
  m_directoryIcon->setPixmap(folderIcon.pixmap(96, 96));
  dirLayout->addWidget(m_directoryIcon, 0, Qt::AlignCenter);

  m_directoryPathLabel = new QLabel(m_directoryPage);
  m_directoryPathLabel->setAlignment(Qt::AlignCenter);
  m_directoryPathLabel->setWordWrap(true);
  m_directoryPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont pathFont = m_directoryPathLabel->font();
  pathFont.setBold(true);
  m_directoryPathLabel->setFont(pathFont);
  dirLayout->addWidget(m_directoryPathLabel);

  m_directoryCountLabel = new QLabel(m_directoryPage);
  m_directoryCountLabel->setAlignment(Qt::AlignCenter);
  m_directoryCountLabel->setStyleSheet(QStringLiteral(
    "QLabel { color: palette(mid); }"));
  dirLayout->addWidget(m_directoryCountLabel);

  dirLayout->addStretch();
  m_stack->addWidget(m_directoryPage);

  // 4〜9: 既存ビュアーを使い回す。
  m_textView     = new TextView(this);
  m_imageView    = new ImageView(this);
  m_binaryView   = new BinaryView(this);
  m_markdownView = new MarkdownView(this);
  m_pdfView      = new PdfView(this);
  m_csvView      = new CsvView(this);
  m_stack->addWidget(m_textView);
  m_stack->addWidget(m_imageView);
  m_stack->addWidget(m_binaryView);
  m_stack->addWidget(m_markdownView);
  m_stack->addWidget(m_pdfView);
  m_stack->addWidget(m_csvView);

  m_stack->setCurrentWidget(m_emptyLabel);
}

void PreviewPane::clear() {
  m_stack->setCurrentWidget(m_emptyLabel);
}

void PreviewPane::showUnsupported(const QString& reason) {
  m_unsupportedLabel->setText(reason);
  m_stack->setCurrentWidget(m_unsupportedLabel);
}

void PreviewPane::showDirectory(const QString& path, int itemCount) {
  m_directoryPathLabel->setText(path);
  if (itemCount < 0) {
    m_directoryCountBase.clear();
  } else if (itemCount == 1) {
    m_directoryCountBase = tr("1 item");
  } else {
    m_directoryCountBase = tr("%1 items").arg(itemCount);
  }
  // size 行はまず「集計中…」プレースホルダで出しておく。すぐに
  // showDirectorySize() で上書きされる。
  if (m_directoryCountBase.isEmpty()) {
    m_directoryCountLabel->setText(QString());
  } else {
    m_directoryCountLabel->setText(m_directoryCountBase
      + QStringLiteral("  ·  ") + tr("calculating size..."));
  }
  m_stack->setCurrentWidget(m_directoryPage);
}

void PreviewPane::showDirectorySize(qint64 totalBytes, int fileCount,
                                     int dirCount, bool finished) {
  if (m_directoryCountBase.isEmpty()) return;   // showDirectory 前なら無視
  if (!m_directoryCountLabel) return;
  const QString sizeStr =
    QLocale(QLocale::English).formattedDataSize(totalBytes);
  // 確定済: "N items · 12.3 MB (15 files, 3 folders)"
  // 走査中: "N items · 12.3 MB (集計中…)"
  // ファイル数 / フォルダ数は走査中も含めて常に最新の暫定値を見せる
  // (PropertiesWorker が 256 件ごとに emit しているため、ある程度滑らかに増える)。
  QString suffix;
  if (finished) {
    if (fileCount > 0 || dirCount > 0) {
      suffix = QStringLiteral(" (%1, %2)")
                 .arg(tr("%n file(s)",   nullptr, fileCount))
                 .arg(tr("%n folder(s)", nullptr, dirCount));
    }
  } else {
    suffix = QStringLiteral(" (%1)").arg(tr("calculating..."));
  }
  m_directoryCountLabel->setText(m_directoryCountBase
    + QStringLiteral("  ·  ") + sizeStr + suffix);
}

void PreviewPane::showLoading() {
  m_stack->setCurrentWidget(m_loadingLabel);
}

void PreviewPane::showText(const TextView::PreparedLoad& prepared) {
  m_textView->applyPreparedLoad(prepared);
  m_stack->setCurrentWidget(m_textView);
}

void PreviewPane::showImage(const ImageView::PreparedLoad& prepared) {
  m_imageView->applyPreparedLoad(prepared);
  m_stack->setCurrentWidget(m_imageView);
}

void PreviewPane::showBinary(const BinaryView::PreparedLoad& prepared) {
  m_binaryView->applyPreparedLoad(prepared);
  m_stack->setCurrentWidget(m_binaryView);
}

void PreviewPane::showMarkdown(const MarkdownView::PreparedLoad& prepared) {
  m_markdownView->applyPreparedLoad(prepared);
  m_stack->setCurrentWidget(m_markdownView);
}

void PreviewPane::showPdf(const PdfView::PreparedLoad& prepared) {
  m_pdfView->applyPreparedLoad(prepared);
  m_stack->setCurrentWidget(m_pdfView);
}

void PreviewPane::showCsv(const CsvView::PreparedLoad& prepared) {
  m_csvView->applyPreparedLoad(prepared);
  m_stack->setCurrentWidget(m_csvView);
}

bool PreviewPane::focusContent() {
  if (!m_stack) return false;
  QWidget* page = m_stack->currentWidget();
  if (!page) return false;
  // Empty / Loading / Unsupported / Directory ページ自体は QLabel なので
  // フォーカスを受け取らない (StrongFocus にしていない)。実ビュアー
  // (Text/Image/Binary/Markdown/Pdf/Csv) のときだけテキスト選択向けに
  // フォーカスを移す。
  if (page == m_textView || page == m_markdownView || page == m_binaryView
      || page == m_csvView || page == m_pdfView || page == m_imageView) {
    page->setFocus(Qt::TabFocusReason);
    // フォーカス取りこぼし対策: 既に focusProxy を設定しているビュアー
    // (TextView の m_editArea 等) は自動で正しい子へ転送する。
    // PreviewPane 全体への Esc 通知を受けたいので、子全部に event filter
    // を仕掛けておく (load 完了の度に呼ばれるのでここで都度 install)。
    installEventFiltersRecursively(page);
    return true;
  }
  return false;
}

bool PreviewPane::focusFirstControl() {
  if (!m_stack) return false;
  QWidget* page = m_stack->currentWidget();
  if (!page) return false;
  // 状態ページ (Empty / Loading / Unsupported / Directory) は focusable な
  // 子を持たない (持ったとしても Cancel ボタンくらいで、Tab 遷移先には
  // したくない)。呼び出し元 (FileManagerPanel ルータ) に false を返して
  // 別の遷移先 (= 自ペインの mode などにラップ) を選ばせる。
  if (page != m_textView && page != m_markdownView && page != m_binaryView
      && page != m_csvView && page != m_pdfView && page != m_imageView) {
    return false;
  }
  // page から forward に walk して PreviewPane 配下で最初の Tab focusable
  // を採用。各ビュアーの setTabOrder で「ツールバー → メイン content」の順に
  // 並んでいる前提。最大歩数は安全策として 1024 段で打ち切り。
  QWidget* w = page;
  for (int i = 0; i < 1024; ++i) {
    QWidget* next = w->nextInFocusChain();
    if (!next || next == page) break;
    if (!isAncestorOf(next)) break;  // ペイン外に出た
    if ((next->focusPolicy() & Qt::TabFocus)
        && next->isVisible() && next->isEnabled()) {
      next->setFocus(Qt::TabFocusReason);
      installEventFiltersRecursively(page);
      return true;
    }
    w = next;
  }
  // 念のためのフォールバック: 何も見つからなければ page (focusProxy) に直接。
  return focusContent();
}

bool PreviewPane::focusLastControl() {
  if (!m_stack) return false;
  QWidget* page = m_stack->currentWidget();
  if (!page) return false;
  if (page != m_textView && page != m_markdownView && page != m_binaryView
      && page != m_csvView && page != m_pdfView && page != m_imageView) {
    return false;
  }
  // page から forward に walk して、PreviewPane 配下で「最後」に登場する
  // Tab focusable な widget を覚えて、その widget にフォーカスを当てる。
  // previousInFocusChain で page から後ろ向きに walk すると先に page の
  // 「外側」(= PreviewPane より前にある widget) に出てしまうので、forward
  // 走査 + 最後を覚える方式にしている。
  QWidget* lastInside = nullptr;
  QWidget* w = page;
  for (int i = 0; i < 1024; ++i) {
    QWidget* next = w->nextInFocusChain();
    if (!next || next == page) break;
    if (!isAncestorOf(next)) break;
    if ((next->focusPolicy() & Qt::TabFocus)
        && next->isVisible() && next->isEnabled()) {
      lastInside = next;
    }
    w = next;
  }
  if (lastInside) {
    lastInside->setFocus(Qt::BacktabFocusReason);
    installEventFiltersRecursively(page);
    return true;
  }
  return focusContent();
}

void PreviewPane::installEventFiltersRecursively(QWidget* root) {
  if (!root) return;
  root->installEventFilter(this);
  const auto children = root->findChildren<QWidget*>();
  for (QWidget* c : children) c->installEventFilter(this);
}

namespace {
// PreviewPane 内で、focused から forward/back 方向に walk して、
// まだ pane 配下に Tab focusable な相手が残っているかを判定。
// 残っていなければ「ペインを抜けるべき」と判断できる。
bool hasFocusableInDirection(const PreviewPane* pane, QWidget* focused,
                              bool back) {
  if (!pane || !focused) return false;
  QWidget* w = focused;
  for (int i = 0; i < 1024; ++i) {
    QWidget* next = back ? w->previousInFocusChain() : w->nextInFocusChain();
    if (!next || next == focused) return false;
    if (!pane->isAncestorOf(next)) return false;
    if ((next->focusPolicy() & Qt::TabFocus)
        && next->isVisible() && next->isEnabled()) {
      return true;
    }
    w = next;
  }
  return false;
}
} // anonymous namespace

bool PreviewPane::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    const int key = ke->key();
    if (key == Qt::Key_Escape) {
      emit escapePressed();
      return true;
    }
    if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
      const bool back = (key == Qt::Key_Backtab)
                        || (ke->modifiers() & Qt::ShiftModifier);
      // 現在フォーカスを持っている widget からチェーンを 1 段進めて、
      // PreviewPane 配下にまだ focusable が残っているかを見る。
      // 残っていなければ「端」と判断してルータに通知する。
      QWidget* focused = QApplication::focusWidget();
      if (focused && isAncestorOf(focused)) {
        if (!hasFocusableInDirection(this, focused, back)) {
          if (back) emit focusExitBackward();
          else      emit focusExitForward();
          event->accept();
          return true;
        }
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

void PreviewPane::setStatusMessage(const QString& msg) {
  // (現状は showUnsupported が直接 setText しているので未使用。
  //  Phase 4 でアイコン付き複合表示にしたいときに使う)
  m_unsupportedLabel->setText(msg);
}

} // namespace Farman
