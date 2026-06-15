#include "FileListThumbnailView.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
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
  // 長いファイル名は中央省略 (例: "very_long_..._name.jpg")。両端から内容が
  // 読めて拡張子が見えるので、サムネイル一覧で識別しやすい。
  setTextElideMode(Qt::ElideMiddle);

  // D&D 周りの設定は FileListView と同じ
  setDragEnabled(true);
  setAcceptDrops(true);
  setDropIndicatorShown(true);
  setDragDropMode(QAbstractItemView::DragDrop);
}

int FileListThumbnailView::gridColumnCount() const {
  const auto* m = model();
  const int n = m ? m->rowCount() : 0;
  if (n <= 1) return qMax(1, n);

  const QRect first = visualRect(m->index(0, 0));
  if (!first.isValid() || gridSize().width() <= 0) {
    // レイアウト未確定時のフォールバック (グリッド幅から概算)。
    const int gw = gridSize().width() > 0 ? gridSize().width() : 1;
    return qMax(1, viewport()->width() / gw);
  }

  // 先頭行に並ぶ要素数 = 列数。visualRect の top が変わるまで数える。
  const int firstTop = first.top();
  int cols = 1;
  for (int i = 1; i < n; ++i) {
    if (visualRect(m->index(i, 0)).top() != firstTop) break;
    ++cols;
  }
  return cols;
}

void FileListThumbnailView::setThumbnailSizePx(int sizePx) {
  setIconSize(QSize(sizePx, sizePx));
  // セル全体サイズを固定する。FileListThumbnailDelegate::sizeHint と完全に
  // 同じ計算式にすることで、delegate が描画する icon / text レイアウトが
  // grid からはみ出さない (= 隣の item のファイル名と被らない)。
  //   kHPadding = 8, kIconTopPad = 4, kIconText = 4 (delegate と同じ)
  //   textHeight = fm.height() * 2     (テキスト最大 2 行 = Finder 風)
  //   余白 = +2 (テキスト下端の microspace)
  const QFontMetrics fm = fontMetrics();
  const int textHeight  = fm.height() * 2;
  const int cellWidth   = sizePx + 2 * 8;
  const int cellHeight  = 4 + sizePx + 4 + textHeight + 2;
  setGridSize(QSize(cellWidth, cellHeight));
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
