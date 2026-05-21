#include "PreviewPane.h"

#include <QLabel>
#include <QStackedWidget>
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

  // 3〜5: 既存ビュアーを使い回す。
  m_textView   = new TextView(this);
  m_imageView  = new ImageView(this);
  m_binaryView = new BinaryView(this);
  m_stack->addWidget(m_textView);
  m_stack->addWidget(m_imageView);
  m_stack->addWidget(m_binaryView);

  m_stack->setCurrentWidget(m_emptyLabel);
}

void PreviewPane::clear() {
  m_stack->setCurrentWidget(m_emptyLabel);
}

void PreviewPane::showUnsupported(const QString& reason) {
  m_unsupportedLabel->setText(reason);
  m_stack->setCurrentWidget(m_unsupportedLabel);
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

void PreviewPane::setStatusMessage(const QString& msg) {
  // (現状は showUnsupported が直接 setText しているので未使用。
  //  Phase 4 でアイコン付き複合表示にしたいときに使う)
  m_unsupportedLabel->setText(msg);
}

} // namespace Farman
