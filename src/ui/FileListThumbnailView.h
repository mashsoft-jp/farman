#pragma once

#include <QListView>
#include <QPoint>
#include <QUrl>
#include <functional>

namespace Farman {

// サムネイルグリッド表示用の QListView 派生。
//
// 役割分担は `FileListView` (QTableView 派生) と同じ:
// - 表示は QListView::IconMode (ResizeMode = Adjust) で、QFileIconProvider /
//   将来的には ThumbnailCache が返す pixmap を Qt::DecorationRole で表示する
//   `FileListModel` を共有して使う。selectionModel は FileListPane が両 View
//   間で共有設定する。
// - **Drag Out (送信側)**: `FileListView` と同じ D&D ロジックを移植 (ファイル
//   一覧の左ボタン押下 → 閾値超で `m_urlsProvider` から URL を取って QDrag 起動)。
// - **Drop In (受信側)**: 外部 / 反対側ペインからの URL ドロップを
//   `externalUrlsDropped` で通知。自分自身からのドロップは無視する。
//
// 視覚的なカーソル / 選択ハイライトは Phase 3 で `FileListThumbnailDelegate`
// を導入してから整える。Phase 0 ではデフォルトの選択描画のままで構わない。
class FileListThumbnailView : public QListView {
  Q_OBJECT

public:
  using UrlsProvider = std::function<QList<QUrl>()>;

  explicit FileListThumbnailView(QWidget* parent = nullptr);

  void setUrlsProvider(UrlsProvider provider) { m_urlsProvider = std::move(provider); }

signals:
  void externalUrlsDropped(const QList<QUrl>& urls);

protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  void startExternalDrag();

  QPoint       m_dragStartPos;
  UrlsProvider m_urlsProvider;
};

} // namespace Farman
