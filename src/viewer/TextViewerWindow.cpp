#include "TextViewerWindow.h"
#include "TextView.h"
#include "utils/CancellableLoadPage.h"   // waitForFutureWithEventLoop + logViewerLoadResult

#include <QApplication>
#include <QFileInfo>
#include <QKeyEvent>
#include <QStackedWidget>
#include <QLabel>
#include <QStatusBar>
#include <QtConcurrent/QtConcurrentRun>

namespace Farman {

TextViewerWindow::TextViewerWindow(const QString& filePath,
                                   const QString& displayPath,
                                   QWidget* parent)
  : QMainWindow(parent)
  , m_filePath(filePath)
  , m_displayPath(displayPath.isEmpty() ? filePath : displayPath)
{
  setupUi();
  loadFile();
}

void TextViewerWindow::setupUi() {
  QFileInfo fileInfo(m_displayPath);
  setWindowTitle(QString("Text Viewer - %1").arg(fileInfo.fileName()));

  m_stack    = new QStackedWidget(this);
  m_loadPage = new CancellableLoadPage(this);
  m_textView = new TextView(this);
  m_stack->addWidget(m_loadPage);
  m_stack->addWidget(m_textView);
  setCentralWidget(m_stack);

  // ロード中の Cancel ボタン / Esc でウィンドウを閉じる。
  connect(m_loadPage, &CancellableLoadPage::cancelled,
          this,        &QMainWindow::close);

  resize(800, 600);
}

void TextViewerWindow::loadFile() {
  m_loadPage->setForFile(m_filePath);
  m_stack->setCurrentWidget(m_loadPage);
  auto token = m_loadPage->resetToken();
  QApplication::setOverrideCursor(Qt::WaitCursor);

  auto future = QtConcurrent::run(&TextView::prepareLoad,
                                   m_filePath,
                                   m_textView->currentUserEncoding(),
                                   token.get(),
                                   /*maxBytes=*/ qint64(-1));
  TextView::PreparedLoad p = waitForFutureWithEventLoop(future);
  QApplication::restoreOverrideCursor();

  const bool cancelled = token && token->load(std::memory_order_acquire);
  if (!p.ok) {
    logViewerLoadResult(QStringLiteral("Text, external"),
                         m_displayPath, false, cancelled);
    // Cancel シグナルで既に close() が走っているケースもあるが、二重 close は
    // 無害。Cancel を経由しない失敗 (file open エラー等) でも close() で
    // ウィンドウを畳む。
    close();
    return;
  }
  m_textView->applyPreparedLoad(p);
  m_stack->setCurrentWidget(m_textView);
  m_textView->setFocus();
  // Inline ビュアーと同じ内容をウィンドウのステータスバーにも表示する。
  statusBar()->addPermanentWidget(new QLabel(m_textView->statusInfo(), this));
  logViewerLoadResult(QStringLiteral("Text, external"),
                       m_displayPath, true, false);
}

void TextViewerWindow::keyPressEvent(QKeyEvent* event) {
  // Esc / Enter / Return でウィンドウを閉じる (Inline モード時の戻り操作と
  // 挙動を揃える)。ロード中の Esc は CancellableLoadPage が先に拾うので、
  // ここに来るのは表示中のとき。close() は WA_DeleteOnClose 付きで生成されて
  // いるのでウィンドウは破棄される。
  if (event->key() == Qt::Key_Escape ||
      event->key() == Qt::Key_Return ||
      event->key() == Qt::Key_Enter) {
    close();
    return;
  }
  QMainWindow::keyPressEvent(event);
}

} // namespace Farman
