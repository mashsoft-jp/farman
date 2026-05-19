#include "FileListThumbnailDelegate.h"
#include "settings/Settings.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBrush>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTextLayout>
#include <QTextOption>

namespace Farman {

namespace {

// セル内のレイアウト定数。FileListThumbnailView::setThumbnailSizePx と
// 完全に揃える必要があり (gridSize の計算根拠)、ずれると text 領域がはみ出る。
constexpr int kHPadding   = 8;   // 左右 padding (片側)
constexpr int kIconTopPad = 4;   // icon 上端の padding
constexpr int kIconText   = 4;   // icon / text 間のスペース
constexpr int kTextLines  = 2;   // テキスト最大行数 (Finder 風)

// 1 つのテキストを最大 maxLines 行に折り返し、はみ出し分を末尾 elide する。
// 描画は (x, y) から topLeft で行い、最終 line は ElideMiddle で中央省略する。
void drawElidedMultilineText(QPainter& p, const QString& text, const QRect& rect,
                              int maxLines, const QFont& font,
                              Qt::Alignment hAlign) {
  if (text.isEmpty() || rect.width() <= 0 || rect.height() <= 0) return;

  const QFontMetrics fm(font);
  const int lineHeight = fm.height();
  const int width      = rect.width();

  QTextLayout layout(text, font);
  QTextOption to;
  to.setAlignment(hAlign | Qt::AlignTop);
  to.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  layout.setTextOption(to);

  layout.beginLayout();
  QList<QTextLine> lines;
  while (true) {
    QTextLine line = layout.createLine();
    if (!line.isValid()) break;
    line.setLineWidth(width);
    lines.append(line);
    if (lines.size() >= maxLines) break;
  }
  layout.endLayout();

  if (lines.isEmpty()) return;

  // 全ての行を描画するが、最終行が text 全体を覆い切っていない場合は
  // その行を ElideMiddle で再描画する (中央省略でファイル名の頭と拡張子を残す)。
  qreal y = rect.top();
  for (int i = 0; i < lines.size(); ++i) {
    const QTextLine& line = lines[i];
    const bool isLast = (i == lines.size() - 1);
    const int textStart = line.textStart();
    const int textEnd   = textStart + line.textLength();
    const bool overflow = isLast && textEnd < text.size();

    if (overflow) {
      // 入りきらなかったので、この行は QTextLine ではなく ElideMiddle で描画。
      const QString lineText = text.mid(textStart);
      const QString elided = fm.elidedText(lineText, Qt::ElideMiddle, width);
      QRectF r(rect.left(), y, width, lineHeight);
      p.drawText(r, hAlign | Qt::AlignTop, elided);
    } else {
      line.draw(&p, QPointF(rect.left(), y));
    }
    y += lineHeight;
  }
}

} // namespace

FileListThumbnailDelegate::FileListThumbnailDelegate(QObject* parent)
  : QStyledItemDelegate(parent) {}

void FileListThumbnailDelegate::paint(QPainter* painter,
                                       const QStyleOptionViewItem& option,
                                       const QModelIndex& index) const {
  if (!painter || !index.isValid()) return;

  const QRect cell = option.rect;

  // ── カーソル判定 (FileListDelegate と同じパターン) ──
  const QAbstractItemView* view =
    qobject_cast<const QAbstractItemView*>(option.widget);
  bool isCurrentRow = false;
  if (view) {
    const QModelIndex cur = view->currentIndex();
    isCurrentRow = cur.isValid() && cur.row() == index.row();
  }

  const Settings& s = Settings::instance();
  QColor cursorColor = s.cursorColor(m_active);
  if (!cursorColor.isValid()) {
    cursorColor = m_active ? Qt::black : Qt::lightGray;
  }

  // ── 背景描画 ──
  // 優先順位: 1) RowBackground 形状のカーソル, 2) 選択 / 比較 etc の
  //          BackgroundRole, 3) 何もしない (transparent)。
  // List モードでは BackgroundRole は QBrush で返るので、QListView もそれを
  // 描画してくれる前提だったが、QStyledItemDelegate::paint を呼ばない経路で
  // 描いているので、ここで明示的に fillRect する必要がある。
  if (isCurrentRow && s.cursorShape() == CursorShape::RowBackground) {
    painter->fillRect(cell, cursorColor);
  } else {
    const QVariant bgVar = index.data(Qt::BackgroundRole);
    if (bgVar.isValid()) {
      QBrush brush;
      if (bgVar.canConvert<QBrush>()) {
        brush = bgVar.value<QBrush>();
      } else if (bgVar.canConvert<QColor>()) {
        brush = QBrush(bgVar.value<QColor>());
      }
      if (brush.color().isValid() && brush.color().alpha() > 0) {
        painter->fillRect(cell, brush);
      }
    }
  }

  // ── 上: icon 領域 (sizePx x sizePx 固定で中央寄せ) ──
  const int iconLeft = cell.left() + (cell.width() - m_sizePx) / 2;
  const int iconTop  = cell.top() + kIconTopPad;
  const QRect iconRect(iconLeft, iconTop, m_sizePx, m_sizePx);

  const QVariant decoVar = index.data(Qt::DecorationRole);
  if (decoVar.isValid()) {
    QIcon icon;
    if (decoVar.canConvert<QIcon>()) {
      icon = decoVar.value<QIcon>();
    } else if (decoVar.canConvert<QPixmap>()) {
      icon = QIcon(decoVar.value<QPixmap>());
    } else if (decoVar.canConvert<QImage>()) {
      icon = QIcon(QPixmap::fromImage(decoVar.value<QImage>()));
    }
    if (!icon.isNull()) {
      const QIcon::Mode mode =
        (option.state & QStyle::State_Selected) ? QIcon::Selected : QIcon::Normal;
      icon.paint(painter, iconRect, Qt::AlignCenter, mode, QIcon::Off);
    }
  }

  // ── 下: text 領域 (最大 2 行で中央寄せ / 中央 elide) ──
  const QFontMetrics fm(option.font);
  const int textHeight = fm.height() * kTextLines;
  const int textTop    = iconRect.bottom() + kIconText;
  const int textLeft   = cell.left() + kHPadding;
  const int textWidth  = cell.width() - 2 * kHPadding;
  const QRect textRect(textLeft, textTop, textWidth, textHeight);

  const QString text = index.data(Qt::DisplayRole).toString();
  if (!text.isEmpty() && textWidth > 0 && textHeight > 0) {
    painter->save();
    // テキスト色は model の ForegroundRole を優先 (FileListModel が category
    // color / 比較 highlight 等を返す)。無効なら palette の Text 色を使う。
    QColor textColor;
    const QVariant fgVar = index.data(Qt::ForegroundRole);
    if (fgVar.canConvert<QBrush>()) {
      textColor = fgVar.value<QBrush>().color();
    } else if (fgVar.canConvert<QColor>()) {
      textColor = fgVar.value<QColor>();
    }
    if (!textColor.isValid()) {
      textColor = option.palette.color(QPalette::Text);
    }
    painter->setPen(textColor);
    painter->setFont(option.font);
    drawElidedMultilineText(*painter, text, textRect, kTextLines,
                            option.font, Qt::AlignHCenter);
    painter->restore();
  }

  // ── Underline 形状はサムネイル表示では「四角く囲む」に置換する ──
  // テキスト下に細い下線を引く List モードの表現は、サムネイルグリッドでは
  // どの item が選択中か視認しにくい。Settings::cursorShape() を尊重しつつ、
  // サムネイル文脈では「item を矩形で囲む」と読み替える。
  // RowBackground 形状は List と同じくセル全体塗り (= 既に上で描画済み)。
  if (isCurrentRow && s.cursorShape() == CursorShape::Underline) {
    painter->save();
    const int thickness = qMax(1, s.cursorThickness());
    QPen pen(cursorColor);
    pen.setWidth(thickness);
    pen.setJoinStyle(Qt::MiterJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    // 太線が cell からはみ出さないよう半幅だけ内側に inset する。
    const int half = thickness / 2;
    const QRect rectInside = cell.adjusted(half, half, -half - (thickness % 2),
                                            -half - (thickness % 2));
    painter->drawRect(rectInside);
    painter->restore();
  }
}

QSize FileListThumbnailDelegate::sizeHint(const QStyleOptionViewItem& option,
                                          const QModelIndex& /*index*/) const {
  // gridSize と矛盾しないよう、cell 全体の高さを正確に返す。
  // (View 側で setGridSize しているのでこちらの sizeHint は補助的だが、
  //  uniformItemSizes 時の最初の item 計算で参照される。)
  const QFontMetrics fm(option.font);
  const int textHeight = fm.height() * kTextLines;
  const int cellWidth  = m_sizePx + 2 * kHPadding;
  const int cellHeight = kIconTopPad + m_sizePx + kIconText + textHeight + 2;
  return QSize(cellWidth, cellHeight);
}

} // namespace Farman
