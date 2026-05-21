#pragma once

#include <QWidget>

#include "viewer/TextView.h"
#include "viewer/ImageView.h"
#include "viewer/BinaryView.h"

class QLabel;
class QStackedWidget;

namespace Farman {

// プレビューモード時に Splitter の右側に常駐するウィジェット。
//   - Dual / Single 中は hide() されており、Preview に切替えた瞬間だけ
//     show() される。
//   - 内部は QStackedWidget で 6 ページを排他切替:
//       0: Empty page          (選択無し / クリア)
//       1: Loading page        (Phase 2 で非同期化したあと使用)
//       2: Unsupported page    (大きすぎる / 非対応 / ディレクトリ等)
//       3: TextView
//       4: ImageView
//       5: BinaryView
//   - PreviewController から show* メソッドを呼ばれて表示を切替える。
//   - フォーカスは保持しない (Phase 1: 左ペインに常時フォーカス)。
class PreviewPane : public QWidget {
  Q_OBJECT
public:
  explicit PreviewPane(QWidget* parent = nullptr);
  ~PreviewPane() override = default;

  // 空表示に戻す。
  void clear();

  // 非対応ファイル / ディレクトリ / 大きすぎ等を、説明テキスト付きで表示。
  void showUnsupported(const QString& reason);

  // 非同期ロード中の表示 (Phase 2 で使う)。
  void showLoading();

  // 既存ビュアーの PreparedLoad を表示に反映する。
  void showText  (const TextView::PreparedLoad& prepared);
  void showImage (const ImageView::PreparedLoad& prepared);
  void showBinary(const BinaryView::PreparedLoad& prepared);

  // 内部ビュアーへの参照 (PreviewController が prepareLoad の引数を
  // 取りに行くために必要)。
  TextView*   textView()   { return m_textView; }
  ImageView*  imageView()  { return m_imageView; }
  BinaryView* binaryView() { return m_binaryView; }

private:
  void setupUi();
  void setStatusMessage(const QString& msg);  // unsupported / loading 共通

  QStackedWidget* m_stack             = nullptr;
  QLabel*         m_emptyLabel        = nullptr;
  QLabel*         m_loadingLabel      = nullptr;
  QLabel*         m_unsupportedLabel  = nullptr;
  TextView*       m_textView          = nullptr;
  ImageView*      m_imageView         = nullptr;
  BinaryView*     m_binaryView        = nullptr;
};

} // namespace Farman
