#include "CancellableLoadPage.h"

#include "core/Logger.h"

#include <QApplication>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace Farman {

CancellableLoadPage::CancellableLoadPage(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 24, 24, 24);
  layout->setSpacing(12);
  layout->addStretch();

  m_title = new QLabel(tr("Loading..."), this);
  QFont f = m_title->font();
  f.setPointSizeF(f.pointSizeF() + 2.0);
  f.setBold(true);
  m_title->setFont(f);
  m_title->setAlignment(Qt::AlignCenter);
  layout->addWidget(m_title);

  m_detail = new QLabel(QString(), this);
  m_detail->setAlignment(Qt::AlignCenter);
  m_detail->setWordWrap(true);
  m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_detail);

  m_bar = new QProgressBar(this);
  m_bar->setRange(0, 0);    // indeterminate
  m_bar->setTextVisible(false);
  m_bar->setMaximumWidth(320);
  auto* barRow = new QHBoxLayout();
  barRow->addStretch();
  barRow->addWidget(m_bar);
  barRow->addStretch();
  layout->addLayout(barRow);

  m_cancelBtn = new QPushButton(tr("Cancel"), this);
  m_cancelBtn->setFocusPolicy(Qt::StrongFocus);
  m_cancelBtn->setAutoDefault(true);
  m_cancelBtn->setDefault(true);
  m_cancelBtn->setMaximumWidth(140);
  connect(m_cancelBtn, &QPushButton::clicked,
          this,         &CancellableLoadPage::cancel);
  auto* cancelRow = new QHBoxLayout();
  cancelRow->addStretch();
  cancelRow->addWidget(m_cancelBtn);
  cancelRow->addStretch();
  layout->addLayout(cancelRow);

  layout->addStretch();
}

void CancellableLoadPage::setForFile(const QString& filePath) {
  const QFileInfo fi(filePath);
  const QString sizeStr = QLocale(QLocale::English).formattedDataSize(fi.size());
  m_title->setText(tr("Loading..."));
  m_detail->setText(QStringLiteral("%1\n%2 — %3")
                       .arg(fi.fileName(), sizeStr, fi.absolutePath()));
}

std::shared_ptr<std::atomic<bool>> CancellableLoadPage::resetToken() {
  m_token = std::make_shared<std::atomic<bool>>(false);
  if (m_cancelBtn) {
    m_cancelBtn->setEnabled(true);
    m_cancelBtn->setFocus(Qt::OtherFocusReason);
  }
  // 表示直後に確実に描画させる (ViewerPanel と同じ作法)。
  QApplication::sendPostedEvents();
  repaint();
  QApplication::processEvents(QEventLoop::AllEvents, 50);
  QApplication::processEvents(QEventLoop::AllEvents, 50);
  return m_token;
}

void CancellableLoadPage::cancel() {
  if (m_token) m_token->store(true, std::memory_order_release);
  if (m_cancelBtn) m_cancelBtn->setEnabled(false);
  emit cancelled();
}

void CancellableLoadPage::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    cancel();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void logViewerLoadResult(const QString& kindLabel,
                          const QString& displayPath,
                          bool           ok,
                          bool           cancelled) {
  auto& log = Logger::instance();
  if (ok) {
    log.info(QStringLiteral("Viewer loaded (%1): %2")
               .arg(kindLabel, displayPath));
  } else if (cancelled) {
    log.info(QStringLiteral("Viewer load cancelled (%1): %2")
               .arg(kindLabel, displayPath));
  } else {
    log.warn(QStringLiteral("Viewer load failed (%1): %2")
               .arg(kindLabel, displayPath));
  }
}

} // namespace Farman
