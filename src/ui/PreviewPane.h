#pragma once

#include <QWidget>

#include "viewer/TextView.h"
#include "viewer/ImageView.h"
#include "viewer/BinaryView.h"
#include "viewer/MarkdownView.h"
#include "viewer/PdfView.h"
#include "viewer/CsvView.h"

class QLabel;
class QStackedWidget;

namespace Farman {

// プレビューモード時に Splitter の右側に常駐するウィジェット。
//   - Dual / Single 中は hide() されており、Preview に切替えた瞬間だけ
//     show() される。
//   - 内部は QStackedWidget で 7 ページを排他切替:
//       0: Empty page          (選択無し / クリア)
//       1: Loading page        (Phase 2 で非同期化したあと使用)
//       2: Unsupported page    (大きすぎる / 非対応 / ディレクトリ等)
//       3: Directory page      (Finder Quick Look 風)
//       4: TextView
//       5: ImageView
//       6: BinaryView
//       7: MarkdownView
//   - PreviewController から show* メソッドを呼ばれて表示を切替える。
//   - フォーカスは保持しない (Phase 1: 左ペインに常時フォーカス)。
class PreviewPane : public QWidget {
  Q_OBJECT
public:
  explicit PreviewPane(QWidget* parent = nullptr);
  ~PreviewPane() override = default;

  // 空表示に戻す。
  void clear();

  // 非対応ファイル / 大きすぎ等を、説明テキスト付きで表示。
  void showUnsupported(const QString& reason);

  // ディレクトリにカーソルがあるときの表示 (Finder Quick Look 風)。
  //   path       : 表示用パス
  //   itemCount  : 「N 個のアイテム」表示用 (-1 で件数行を省略)
  // 件数行は内部に覚えておき、後から showDirectorySize() で再帰サイズの
  // 行末追記ができる。表示直後は size 行が「(集計中…)」プレースホルダ。
  void showDirectory(const QString& path, int itemCount);

  // ディレクトリ走査結果 (PropertiesWorker からのストリーミング更新) を
  // 件数行の末尾に追記する。`finished=true` で「集計中…」を取り除き、
  // 確定の合計サイズ + ファイル / ディレクトリ件数を表示する。
  // showDirectory() で表示中でない場合は何もしない。
  void showDirectorySize(qint64 totalBytes, int fileCount, int dirCount,
                          bool finished);

  // 非同期ロード中の表示 (Phase 2 で使う)。
  void showLoading();

  // 既存ビュアーの PreparedLoad を表示に反映する。
  void showText    (const TextView::PreparedLoad&     prepared);
  void showImage   (const ImageView::PreparedLoad&    prepared);
  void showBinary  (const BinaryView::PreparedLoad&   prepared);
  void showMarkdown(const MarkdownView::PreparedLoad& prepared);
  void showPdf     (const PdfView::PreparedLoad&      prepared);
  void showCsv     (const CsvView::PreparedLoad&      prepared);

  // 内部ビュアーへの参照 (PreviewController が prepareLoad の引数を
  // 取りに行くために必要)。
  TextView*     textView()     { return m_textView; }
  ImageView*    imageView()    { return m_imageView; }
  BinaryView*   binaryView()   { return m_binaryView; }
  MarkdownView* markdownView() { return m_markdownView; }
  PdfView*      pdfView()      { return m_pdfView; }
  CsvView*      csvView()      { return m_csvView; }

  // ファイルリスト側から Tab で呼び出される: 現在 m_stack に表示中のビュアー
  // にフォーカスを当てる (テキスト選択 / コピー目的)。ロード中 / 空表示の
  // ページにはフォーカスを当てない (Cancel ボタンが拾ってしまうため)。
  // 返り値: フォーカスを移せたら true。
  bool focusContent();

  // ファイルリストペイン → プレビュー先頭/末尾。Tab 連鎖の外部入口。
  //   - focusFirstControl: 現在表示中のページ配下で、フォーカスチェーン上で
  //                        最も前にある Tab focusable な widget にフォーカス。
  //                        典型的には「ツールバーの左端ボタン」。
  //   - focusLastControl : 同チェーン上で最も後ろの widget にフォーカス。
  //                        典型的にはメインコンテンツ (本文 / 画像 等)。
  // 状態ページ (Empty / Loading / Unsupported / Directory) では false を返す
  // (フォーカスを受ける相手が居ないので、呼び出し側は別の遷移先を選ぶ)。
  bool focusFirstControl();
  bool focusLastControl();

signals:
  // プレビューペイン内で Esc が押された → 呼び出し側 (FileManagerPanel) に
  // 「ファイルリストへ戻して欲しい」と伝える。
  void escapePressed();

  // プレビュー内の Tab 連鎖の端 (= 末尾で Tab / 先頭で Shift+Tab) に
  // 到達したことを通知する。FileManagerPanel が router でアクティブな
  // FileListPane の ★ / mode に戻す。
  void focusExitForward();    // 末尾で Tab     → 自ペイン ★ にラップ
  void focusExitBackward();   // 先頭で Shift+Tab → 自ペイン mode にラップ

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  // focusContent 内で使う: 与えたウィジェットとその全子孫に `this` を
  // event filter として install する。テキストビュアー内の QPlainTextEdit
  // などに対しても Esc を拾えるようにするため。
  void installEventFiltersRecursively(QWidget* root);

private:
  void setupUi();
  void setStatusMessage(const QString& msg);  // unsupported / loading 共通

  QStackedWidget* m_stack             = nullptr;
  QLabel*         m_emptyLabel        = nullptr;
  QLabel*         m_loadingLabel      = nullptr;
  QLabel*         m_unsupportedLabel  = nullptr;
  // ディレクトリ専用ページ (アイコン + パス + 件数の縦並び)。
  QWidget*        m_directoryPage     = nullptr;
  QLabel*         m_directoryIcon     = nullptr;
  QLabel*         m_directoryPathLabel  = nullptr;
  QLabel*         m_directoryCountLabel = nullptr;
  // 浅い entryList で取った直近の表示用テキスト ("1 item" / "%1 items" / "")。
  // showDirectorySize() で append するときに頭の N items 部分を再利用する。
  QString         m_directoryCountBase;
  TextView*       m_textView          = nullptr;
  ImageView*      m_imageView         = nullptr;
  BinaryView*     m_binaryView        = nullptr;
  MarkdownView*   m_markdownView      = nullptr;
  PdfView*        m_pdfView           = nullptr;
  CsvView*        m_csvView           = nullptr;
};

} // namespace Farman
