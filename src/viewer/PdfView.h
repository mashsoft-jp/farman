#pragma once

#include <QString>
#include <QWidget>

#include <atomic>

class QPdfDocument;
class QPdfSearchModel;
class QPdfView;
class QSpinBox;
class QLabel;
class QLineEdit;
class QToolBar;
class QToolButton;

namespace Farman {

// PDF ファイル表示用ウィジェット。
//
//   - Qt PDF モジュール (`QPdfView` + `QPdfDocument`) で実装。PDFium ベース。
//   - ツールバー: 前ページ / 次ページ / ページジャンプ / ズーム +- /
//     ページ幅にフィット / ページ全体にフィット / モード切替 (Single / Continuous) /
//     全文検索 (QPdfSearchModel)。
//   - テキスト選択 / 回転は Phase 1 ではスコープ外 (将来課題)。回転は
//     QPdfView 単体では実現できず、QPdfDocumentRenderOptions を介した
//     カスタム描画が必要なため Phase 2 以降。
//   - 大きい PDF (数百ページ) でも QPdfView は遅延描画なので初回ロードは
//     ヘッダ解析だけ。本体ページのレンダリングはスクロールで visible に
//     なったときに走る。
class PdfView : public QWidget {
  Q_OBJECT

public:
  // 非同期ロード用の中間表現。PDF は QPdfDocument を UI スレッドで生成する
  // 必要があるので、ワーカーでは「ファイルが存在し、サイズが取れること」
  // しか確認しない。実ロードは applyPreparedLoad で行う。
  struct PreparedLoad {
    bool    ok = false;
    QString filePath;
    qint64  fileSize = 0;
  };

  explicit PdfView(QWidget* parent = nullptr);
  ~PdfView() override;

  bool loadFile(const QString& filePath);

  // ワーカースレッド対応のロード関数。実 PDF ロードは applyPreparedLoad
  // の中で UI スレッドからやるので、ここでは存在チェックとサイズ取得のみ。
  static PreparedLoad prepareLoad(const QString& filePath,
                                  const std::atomic<bool>* cancelToken = nullptr);
  void applyPreparedLoad(const PreparedLoad& result);

  // 表示中ファイルをクリア
  void clearContent();

  // ステータスバー表示用の要約 ("PDF · N pages · file size")
  QString statusInfo() const;

protected:
  // Cmd/Ctrl+F のグローバルキャプチャ用。フォーカスが他にあっても拾えるよう
  // qApp->installEventFilter で全アプリにフックする。
  bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
  void onPrevPage();
  void onNextPage();
  void onPageSpinChanged(int displayPage);   // 1-based
  void onZoomIn();
  void onZoomOut();
  void onFitWidth(bool checked);
  void onFitPage(bool checked);
  void onPageModeToggled(bool continuous);
  void onCurrentPageChanged(int page);       // 0-based from QPdfPageNavigator
  void onPageCountChanged(int pageCount);
  void onFindTextChanged(const QString& text);
  void onFindCountChanged();
  void findNext();
  void findPrevious();
  void focusFindInput();

private:
  void setupUi();
  void updatePageLabel();
  void jumpToSearchResult(int index);
  void updateFindStatus();

  QToolBar*     m_toolbar       = nullptr;
  QToolButton*  m_prevButton    = nullptr;
  QToolButton*  m_nextButton    = nullptr;
  QSpinBox*     m_pageSpin      = nullptr;
  QLabel*       m_pageTotalLabel = nullptr;
  QToolButton*  m_zoomOutButton = nullptr;
  QToolButton*  m_zoomInButton  = nullptr;
  QToolButton*  m_fitWidthButton = nullptr;
  QToolButton*  m_fitPageButton  = nullptr;
  QToolButton*  m_continuousButton = nullptr;

  // 検索 (QPdfSearchModel ベース)。QPdfView::setSearchModel でビューに渡すと
  // 全件ハイライトが入る。setCurrentSearchResultIndex で現在ヒットへスクロール。
  QLineEdit*       m_findEdit    = nullptr;
  QToolButton*     m_findPrevBtn = nullptr;
  QToolButton*     m_findNextBtn = nullptr;
  QLabel*          m_findStatus  = nullptr;
  QPdfSearchModel* m_searchModel = nullptr;
  int              m_currentSearchIndex = -1;

  QPdfView*     m_view     = nullptr;
  QPdfDocument* m_document = nullptr;

  QString m_filePath;
  qint64  m_fileSize = 0;
};

} // namespace Farman
