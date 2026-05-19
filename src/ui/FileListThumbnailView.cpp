#include "FileListThumbnailView.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>

namespace Farman {

FileListThumbnailView::FileListThumbnailView(QWidget* parent) : QListView(parent) {
  setViewMode(QListView::IconMode);
  setResizeMode(QListView::Adjust);
  setMovement(QListView::Static);
  setUniformItemSizes(true);
  setWordWrap(true);
  setSpacing(8);
  setSelectionBehavior(QAbstractItemView::SelectRows);
  setFrameShape(QFrame::NoFrame);

  // D&D 周りの設定は FileListView と同じ
  setDragEnabled(true);
  setAcceptDrops(true);
  setDropIndicatorShown(true);
  setDragDropMode(QAbstractItemView::DragDrop);
}

void FileListThumbnailView::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    m_dragStartPos = event->pos();
  }
  QListView::mousePressEvent(event);
}

void FileListThumbnailView::mouseMoveEvent(QMouseEvent* event) {
  if ((event->buttons() & Qt::LeftButton) && m_urlsProvider) {
    if ((event->pos() - m_dragStartPos).manhattanLength()
        >= QApplication::startDragDistance()) {
      startExternalDrag();
      return;
    }
  }
  QListView::mouseMoveEvent(event);
}

void FileListThumbnailView::startExternalDrag() {
  const QList<QUrl> urls = m_urlsProvider();
  if (urls.isEmpty()) return;

  auto* mime = new QMimeData;
  mime->setUrls(urls);
  auto* drag = new QDrag(this);
  drag->setMimeData(mime);
  drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
}

void FileListThumbnailView::dragEnterEvent(QDragEnterEvent* event) {
  if (event->mimeData()->hasUrls() && event->source() != this) {
    event->acceptProposedAction();
  } else {
    QListView::dragEnterEvent(event);
  }
}

void FileListThumbnailView::dragMoveEvent(QDragMoveEvent* event) {
  if (event->mimeData()->hasUrls() && event->source() != this) {
    event->acceptProposedAction();
  } else {
    QListView::dragMoveEvent(event);
  }
}

void FileListThumbnailView::dropEvent(QDropEvent* event) {
  if (!event->mimeData()->hasUrls() || event->source() == this) {
    QListView::dropEvent(event);
    return;
  }
  emit externalUrlsDropped(event->mimeData()->urls());
  event->acceptProposedAction();
}

} // namespace Farman
