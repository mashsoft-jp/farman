#include "PreviewPane.h"

#include <QApplication>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QLabel>
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
    m_directoryCountLabel->setText(QString());
  } else if (itemCount == 1) {
    m_directoryCountLabel->setText(tr("1 item"));
  } else {
    m_directoryCountLabel->setText(tr("%1 items").arg(itemCount));
  }
  m_stack->setCurrentWidget(m_directoryPage);
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

void PreviewPane::setStatusMessage(const QString& msg) {
  // (現状は showUnsupported が直接 setText しているので未使用。
  //  Phase 4 でアイコン付き複合表示にしたいときに使う)
  m_unsupportedLabel->setText(msg);
}

} // namespace Farman
