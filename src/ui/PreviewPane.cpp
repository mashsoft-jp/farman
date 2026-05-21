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

  // Phase 0: 単なるプレースホルダ。
  m_placeholderLabel = new QLabel(tr("Preview mode (under construction)"), this);
  m_placeholderLabel->setAlignment(Qt::AlignCenter);
  m_placeholderLabel->setStyleSheet(QStringLiteral(
    "QLabel { color: palette(mid); padding: 16px; }"));
  m_stack->addWidget(m_placeholderLabel);
  m_stack->setCurrentWidget(m_placeholderLabel);
}

void PreviewPane::clear() {
  // Phase 0: 何もしない (常に placeholder を表示)。
}

} // namespace Farman
