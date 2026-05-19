#pragma once

#include <QStyledItemDelegate>

namespace Farman {

// サムネイル表示モード用の itemDelegate。
//
// QListView::IconMode の標準描画は、各 item の icon を「実際の pixmap サイズ」
// で描き、テキストはその直下に置く。結果として画像のアスペクト比 (縦長 /
// 横長) によってテキストの縦位置がばらつき、Finder のような整列したグリッド
// にならない。また、縦長画像が grid セルの icon 領域 (sizePx) を超えると
// テキスト領域に被って読めなくなる。
//
// この delegate は cell rect を明示的に「上 = アイコン領域 (固定 sizePx)」/
// 「下 = テキスト領域 (固定 2 行分)」に分割し、icon は icon 領域に center fit、
// text は最大 2 行で elide (はみ出しを中央省略) する。これで全 item の
// テキスト y 位置が揃い、ファイル名が画像に隠れない。
class FileListThumbnailDelegate : public QStyledItemDelegate {
  Q_OBJECT

public:
  explicit FileListThumbnailDelegate(QObject* parent = nullptr);

  // 上部 icon 領域の sizePx を設定する。FileListPane::setViewMode 経由で
  // 呼ばれ、ThumbnailView::setThumbnailSizePx と同期する。
  void setThumbnailSizePx(int sizePx) { m_sizePx = sizePx; }
  int  thumbnailSizePx() const { return m_sizePx; }

  // ペインのアクティブ状態。List 側の FileListDelegate と揃え、カーソル色を
  // Settings::cursorColor(active) から取得する。
  void setActive(bool active) { m_active = active; }
  bool isActive() const { return m_active; }

  void paint(QPainter* painter,
             const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;

  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;

private:
  int  m_sizePx = 160;
  bool m_active = true;
};

} // namespace Farman
