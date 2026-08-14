#include "ViewerPanel.h"
#include "core/Logger.h"
#include "settings/Settings.h"
#include "utils/MediaMatchers.h"
#include "utils/CancellableLoadPage.h"   // logViewerLoadResult
#include "viewer/BinaryView.h"
#include "viewer/CsvView.h"
#include "viewer/ImageView.h"
#include "viewer/IViewerPlugin.h"
#include "viewer/MarkdownView.h"
#include "viewer/PdfView.h"
#include "viewer/TextView.h"
#include "viewer/ViewerDispatcher.h"
#include "keybinding/ViewerKeyBindingManager.h"
#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMimeDatabase>
#include <QMimeType>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace Farman {

ViewerPanel::ViewerPanel(QWidget* parent)
  : QWidget(parent)
{
  setupUi();
}

ViewerPanel::~ViewerPanel() = default;

void ViewerPanel::setupUi() {
  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_stack = new QStackedWidget(this);
  layout->addWidget(m_stack);

  // ===== Text Viewer =====
  m_textView = new TextView(this);
  m_stack->addWidget(m_textView);

  // ===== Image Viewer =====
  m_imageView = new ImageView(this);
  m_stack->addWidget(m_imageView);

  // ===== Binary Viewer (fallback) =====
  m_binaryView = new BinaryView(this);
  m_stack->addWidget(m_binaryView);
  // カーソル移動でステータス (位置 / 全体) が変わったら本体ステータスバーへ中継。
  connect(m_binaryView, &BinaryView::statusInfoChanged,
          this,          &ViewerPanel::onPluginStatusInfoChanged);

  // ===== Markdown Viewer (.md / .markdown 等) =====
  m_markdownView = new MarkdownView(this);
  m_stack->addWidget(m_markdownView);

  // ===== PDF Viewer (.pdf) =====
  m_pdfView = new PdfView(this);
  m_stack->addWidget(m_pdfView);

  // ===== CSV / TSV Viewer =====
  m_csvView = new CsvView(this);
  m_stack->addWidget(m_csvView);

  // 組込み 6 ビュアーへ、本体キーバインド設定のショートカット割り当てを push する
  // （一方向：本体→ビュアー）。これらのビュアーは常駐で使い回されるため、生成時に
  // 一度 push し、以後は設定変更シグナルで再 push する。
  reapplyViewerShortcuts();
  connect(&ViewerKeyBindingManager::instance(),
          &ViewerKeyBindingManager::bindingsChanged,
          this, &ViewerPanel::reapplyViewerShortcuts);

  // ===== Loading placeholder =====
  // 大きいファイルや行数の多いテキストの読み込み中、ユーザーに「何も
  // 起きていない」と思わせないためのプレースホルダ。indeterminate な
  // QProgressBar を出して動いている感を出す (実際のロードは同期的なので
  // 動きは限定的だが、画面が真っ白で固まったように見えるのは防げる)。
  m_loadingPage = new QWidget(this);
  QVBoxLayout* loadingLayout = new QVBoxLayout(m_loadingPage);
  loadingLayout->setContentsMargins(24, 24, 24, 24);
  loadingLayout->setSpacing(12);
  loadingLayout->addStretch();

  m_loadingTitle = new QLabel(tr("Loading..."), m_loadingPage);
  QFont f = m_loadingTitle->font();
  f.setPointSizeF(f.pointSizeF() + 2.0);
  f.setBold(true);
  m_loadingTitle->setFont(f);
  m_loadingTitle->setAlignment(Qt::AlignCenter);
  loadingLayout->addWidget(m_loadingTitle);

  m_loadingDetail = new QLabel(QString(), m_loadingPage);
  m_loadingDetail->setAlignment(Qt::AlignCenter);
  m_loadingDetail->setWordWrap(true);
  m_loadingDetail->setTextInteractionFlags(Qt::TextSelectableByMouse);
  loadingLayout->addWidget(m_loadingDetail);

  m_loadingBar = new QProgressBar(m_loadingPage);
  m_loadingBar->setRange(0, 0);   // indeterminate
  m_loadingBar->setTextVisible(false);
  m_loadingBar->setMaximumWidth(320);
  // 中央寄せ用に水平レイアウトにラップ
  QHBoxLayout* barRow = new QHBoxLayout();
  barRow->addStretch();
  barRow->addWidget(m_loadingBar);
  barRow->addStretch();
  loadingLayout->addLayout(barRow);

  // Cancel ボタン。ロード中の状態でフォーカスを当てるので、Enter で
  // 即キャンセル → ファイルマネージャに戻れる。Esc も keyPressEvent で
  // ここに転送する。
  m_loadingCancelButton = new QPushButton(tr("Cancel"), m_loadingPage);
  m_loadingCancelButton->setFocusPolicy(Qt::StrongFocus);
  m_loadingCancelButton->setAutoDefault(true);
  m_loadingCancelButton->setDefault(true);
  m_loadingCancelButton->setMaximumWidth(140);
  connect(m_loadingCancelButton, &QPushButton::clicked,
          this,                    &ViewerPanel::cancelCurrentLoad);
  QHBoxLayout* cancelRow = new QHBoxLayout();
  cancelRow->addStretch();
  cancelRow->addWidget(m_loadingCancelButton);
  cancelRow->addStretch();
  loadingLayout->addLayout(cancelRow);

  loadingLayout->addStretch();
  m_stack->addWidget(m_loadingPage);
}

bool ViewerPanel::viewerKindFromPluginId(const QString& pluginId, ViewerKind& kind) {
  if (pluginId == QLatin1String("text_viewer"))     { kind = ViewerKind::Text;     return true; }
  if (pluginId == QLatin1String("image_viewer"))    { kind = ViewerKind::Image;    return true; }
  if (pluginId == QLatin1String("binary_viewer"))   { kind = ViewerKind::Binary;   return true; }
  if (pluginId == QLatin1String("markdown_viewer")) { kind = ViewerKind::Markdown; return true; }
  if (pluginId == QLatin1String("pdf_viewer"))      { kind = ViewerKind::Pdf;      return true; }
  if (pluginId == QLatin1String("csv_viewer"))      { kind = ViewerKind::Csv;      return true; }
  return false;
}

ViewerPanel::ViewerKind ViewerPanel::resolveAuto(const QString& filePath) {
  // 判定は ViewerDispatcher::resolvePlugin() に一本化する。
  //
  // かつてはここに「画像(拡張子|MIME) → PDF → CSV → Markdown →
  // テキスト(拡張子|MIME) → バイナリ」という独自の固定チェーンを持っていたが、
  // resolvePlugin とは規則が違った (MIME をフォールバックではなく拡張子と同列に
  // 見ていたので、画像の MIME 一致が PDF/CSV/Markdown の拡張子一致に勝った。
  // プラグインの priority も見ていなかった)。同じファイルでも経路によって選ばれる
  // ビュアーが変わりうる状態だったので、ルールを 1 本にまとめた。
  if (IViewerPlugin* plugin =
        ViewerDispatcher::instance().resolvePlugin(filePath)) {
    ViewerKind kind;
    if (viewerKindFromPluginId(plugin->pluginId(), kind)) {
      return kind;
    }
  }
  // 内蔵 ViewerKind を持たないプラグイン (media / 外部) と、どのプラグインにも
  // 当たらなかった場合は最後のフォールバックであるバイナリで開く。
  return ViewerKind::Binary;
}

namespace {
// ビュアーの表示名をローカライズする。プラグイン名 (pluginName) と同じ
// 共有コンテキスト "ViewerNames" で translate するので、farman 本体・各
// プラグイン (別ライブラリ) のどちらから呼んでも同じ farman_ja.qm で解決される。
// ここ (farman 本体) が全 7 文字列を参照することで lupdate が farman_ja.ts に
// 取り込む。外部プラグインは fallback (プラグイン自身の名前) を使う。
QString localizedViewerName(const QString& pluginId, const QString& fallback) {
  if (pluginId == QLatin1String("text_viewer")) {
    return QCoreApplication::translate("ViewerNames", "Text Viewer");
  }
  if (pluginId == QLatin1String("image_viewer")) {
    return QCoreApplication::translate("ViewerNames", "Image Viewer");
  }
  if (pluginId == QLatin1String("binary_viewer")) {
    return QCoreApplication::translate("ViewerNames", "Binary Viewer");
  }
  if (pluginId == QLatin1String("markdown_viewer")) {
    return QCoreApplication::translate("ViewerNames", "Markdown Viewer");
  }
  if (pluginId == QLatin1String("pdf_viewer")) {
    return QCoreApplication::translate("ViewerNames", "PDF Viewer");
  }
  if (pluginId == QLatin1String("csv_viewer")) {
    return QCoreApplication::translate("ViewerNames", "CSV/TSV Viewer");
  }
  if (pluginId == QLatin1String("media_viewer")) {
    return QCoreApplication::translate("ViewerNames", "Media Viewer");
  }
  return fallback;
}

QString kindToPluginId(ViewerPanel::ViewerKind kind) {
  switch (kind) {
    case ViewerPanel::ViewerKind::Text:     return QStringLiteral("text_viewer");
    case ViewerPanel::ViewerKind::Image:    return QStringLiteral("image_viewer");
    case ViewerPanel::ViewerKind::Binary:   return QStringLiteral("binary_viewer");
    case ViewerPanel::ViewerKind::Markdown: return QStringLiteral("markdown_viewer");
    case ViewerPanel::ViewerKind::Pdf:      return QStringLiteral("pdf_viewer");
    case ViewerPanel::ViewerKind::Csv:      return QStringLiteral("csv_viewer");
    case ViewerPanel::ViewerKind::Auto:     return QString();
  }
  return QString();
}
}  // namespace

bool ViewerPanel::openFile(const QString& filePath, ViewerKind kind,
                           const QString& displayPath) {
  if (filePath.isEmpty()) {
    return false;
  }

  QFileInfo fileInfo(filePath);
  if (!fileInfo.exists() || !fileInfo.isFile()) {
    return false;
  }

  // ステータス・currentFilePath として記録する「表示用パス」。
  // displayPath が空ならディスク上のパス (filePath) をそのまま使う。
  const QString pathForStatus = displayPath.isEmpty() ? filePath : displayPath;

  // ロード前にプレースホルダを出して 1 回再描画させる。
  showLoadingState(pathForStatus);
  QApplication::setOverrideCursor(Qt::WaitCursor);

  // Auto は resolveAuto() に委譲してから個別の openXxxFile に振る。
  // 拡張子 / MIME ルーティングは「表示用パス」を尊重 (アーカイブ内エントリの
  // 元拡張子で振り分けたいので)。
  if (kind == ViewerKind::Auto) {
    IViewerPlugin* plugin = ViewerDispatcher::instance().resolvePlugin(pathForStatus);
    if (plugin) {
      if (ViewerDispatcher::instance().isExternalPlugin(plugin->pluginId())) {
        const bool ok = openPluginFile(plugin, filePath, pathForStatus);
        QApplication::restoreOverrideCursor();
        return ok;
      }
      if (!viewerKindFromPluginId(plugin->pluginId(), kind)) {
        // 内蔵ビュー (ViewerKind) を持たない同梱公式プラグイン
        // (media_viewer 等) は外部プラグインと同じ埋め込み経路で表示する。
        const bool ok = openPluginFile(plugin, filePath, pathForStatus);
        QApplication::restoreOverrideCursor();
        return ok;
      }
    } else {
      kind = resolveAuto(pathForStatus);
    }
  }

  clearPluginView();

  // タイトルバー用に、開くビュアーのローカライズ名を記録する。
  m_currentViewerName = localizedViewerName(kindToPluginId(kind), QString());

  bool ok = false;
  switch (kind) {
    case ViewerKind::Text:     ok = openTextFile(filePath, pathForStatus);     break;
    case ViewerKind::Image:    ok = openImageFile(filePath, pathForStatus);    break;
    case ViewerKind::Binary:   ok = openBinaryFile(filePath, pathForStatus);   break;
    case ViewerKind::Markdown: ok = openMarkdownFile(filePath, pathForStatus); break;
    case ViewerKind::Pdf:      ok = openPdfFile(filePath, pathForStatus);      break;
    case ViewerKind::Csv:      ok = openCsvFile(filePath, pathForStatus);      break;
    case ViewerKind::Auto:     /* unreachable */ break;
  }

  QApplication::restoreOverrideCursor();
  return ok;
}

bool ViewerPanel::openWithPlugin(const QString& filePath, const QString& pluginId,
                                 const QString& displayPath) {
  // 内蔵 ViewerKind を持つ同梱プラグインは既存の openFile 経路で開く
  // (検索バー等を備えた内蔵ビューをそのまま使う)。
  ViewerKind kind;
  if (viewerKindFromPluginId(pluginId, kind)) {
    return openFile(filePath, kind, displayPath);
  }

  // ViewerKind を持たない (media 等 / 外部) プラグインは埋め込み経路で開く。
  IViewerPlugin* plugin = ViewerDispatcher::instance().pluginById(pluginId);
  if (!plugin || filePath.isEmpty()) {
    return false;
  }
  const QFileInfo fileInfo(filePath);
  if (!fileInfo.exists() || !fileInfo.isFile()) {
    return false;
  }
  const QString pathForStatus = displayPath.isEmpty() ? filePath : displayPath;
  showLoadingState(pathForStatus);
  QApplication::setOverrideCursor(Qt::WaitCursor);
  const bool ok = openPluginFile(plugin, filePath, pathForStatus);
  QApplication::restoreOverrideCursor();
  return ok;
}

void ViewerPanel::clearPluginView() {
  if (!m_pluginView) return;
  if (focusProxy() == m_pluginView) {
    setFocusProxy(nullptr);
  }
  m_stack->removeWidget(m_pluginView);
  m_pluginView->deleteLater();
  m_pluginView = nullptr;
  m_pluginViewPlugin = nullptr;
}

void ViewerPanel::reapplyViewerShortcuts() {
  // 組込み 6 ビュアー（常駐）へ viewerId 指定で push。
  pushViewerShortcutBindings(m_textView,     QStringLiteral("text"));
  pushViewerShortcutBindings(m_imageView,    QStringLiteral("image"));
  pushViewerShortcutBindings(m_binaryView,   QStringLiteral("binary"));
  pushViewerShortcutBindings(m_markdownView, QStringLiteral("markdown"));
  pushViewerShortcutBindings(m_pdfView,      QStringLiteral("pdf"));
  pushViewerShortcutBindings(m_csvView,      QStringLiteral("csv"));
  // 表示中のプラグインビュアー（media / 外部）があれば、その取得 API 経由で push。
  if (m_pluginView && m_pluginViewPlugin) {
    pushViewerShortcutBindings(m_pluginView, m_pluginViewPlugin);
  }
}

void ViewerPanel::showLoadingState(const QString& filePath) {
  const QFileInfo fi(filePath);
  m_loadingTitle->setText(tr("Loading..."));
  const QString sizeStr = QLocale(QLocale::English)
                            .formattedDataSize(fi.size());
  // ファイル名 (太字相当) と サイズ・パスを 2 行で。
  m_loadingDetail->setText(QStringLiteral("%1\n%2 — %3")
                             .arg(fi.fileName(), sizeStr, fi.absolutePath()));

  // 新しい cancel token を発行。openXxxFile はこの token の生ポインタを
  // prepareLoad に渡して、ユーザーが Cancel を押したら早期 return させる。
  m_currentCancelToken = std::make_shared<std::atomic<bool>>(false);

  m_stack->setCurrentWidget(m_loadingPage);
  // ロード中はキャンセル操作以外受けつけたくないので、Cancel ボタンに
  // フォーカスを当てる。Enter で即キャンセル (default ボタン)。Esc も
  // keyPressEvent でキャンセルに転送する。
  if (m_loadingCancelButton) {
    m_loadingCancelButton->setEnabled(true);
    m_loadingCancelButton->setFocus(Qt::OtherFocusReason);
  }

  // 確実に画面に出すための強制再描画。
  // - processEvents(ExcludeUserInputEvents) だけでは初回表示時 (レイアウト
  //   が未確定) に paint が間に合わないことがあるため、AllEvents で時間を
  //   与えつつ、子ウィジェットも含めて同期 repaint する。
  // - ProgressBar を含めて 2 回 processEvents を回し、1 回目の paint で
  //   生成されたフォローアップ描画イベント (子ウィジェット) も拾わせる。
  QApplication::sendPostedEvents();
  m_loadingPage->repaint();
  m_loadingTitle->repaint();
  m_loadingDetail->repaint();
  m_loadingBar->repaint();
  QApplication::processEvents(QEventLoop::AllEvents, 50);
  QApplication::processEvents(QEventLoop::AllEvents, 50);
}

void ViewerPanel::cancelCurrentLoad() {
  // worker (prepareLoad) はトークンを定期的に見ているので、true をセット
  // すれば早期 return して waitForFutureWithEventLoop が抜ける。
  if (m_currentCancelToken) {
    m_currentCancelToken->store(true, std::memory_order_release);
  }
  // 連打で複数 cancel が走らないようにボタンを一旦無効化 (実際の遷移は
  // openXxxFile 側の戻り処理に任せる)。
  if (m_loadingCancelButton) m_loadingCancelButton->setEnabled(false);
}

void ViewerPanel::keyPressEvent(QKeyEvent* event) {
  // ロード中ページ表示中の Esc キーは Cancel ボタンに転送する。Cancel
  // ボタンが default になっているので Enter は自動でクリック扱いになる。
  if (m_stack && m_stack->currentWidget() == m_loadingPage
      && event->key() == Qt::Key_Escape) {
    cancelCurrentLoad();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

// 各ビュアーの prepareLoad をワーカースレッドで実行し、戻り値を待つ間
// メインスレッドのイベントループを回す。
//   - waitForFutureWithEventLoop / logViewerLoadResult は
//     utils/CancellableLoadPage.h で共通化されている (External の各
//     *ViewerWindow とも同じものを使う)。
//   - 入力イベントは流す (= ユーザーが Cancel ボタンを押せる / Esc が届く)。
//     ExcludeUserInputEvents にすると Cancel が効かなくなるので注意。

bool ViewerPanel::openTextFile(const QString& filePath, const QString& displayPath) {
  auto token = m_currentCancelToken;
  auto future = QtConcurrent::run(&TextView::prepareLoad,
                                   filePath,
                                   m_textView->currentUserEncoding(),
                                   token ? token.get() : nullptr,
                                   /*maxBytes=*/ qint64(-1));
  TextView::PreparedLoad p = waitForFutureWithEventLoop(future);
  const bool cancelled = token && token->load(std::memory_order_acquire);
  if (!p.ok) {
    logViewerLoadResult(QStringLiteral("Text"), displayPath, false, cancelled);
    return false;
  }

  m_textView->applyPreparedLoad(p);
  m_stack->setCurrentWidget(m_textView);
  setFocusProxy(m_textView);
  m_currentFilePath = displayPath;
  emit fileOpened(displayPath);
  emit viewerStatusChanged(displayPath, m_textView->statusInfo());
  logViewerLoadResult(QStringLiteral("Text"), displayPath, true, false);
  return true;
}

bool ViewerPanel::openImageFile(const QString& filePath, const QString& displayPath) {
  // Image は prepareLoad に cancelToken 引数が無い (QImageReader は中断不可)。
  // ロード後に token を見て、Cancel ボタンが押されていたら結果を捨てる。
  auto token = m_currentCancelToken;
  auto future = QtConcurrent::run(&ImageView::prepareLoad, filePath);
  ImageView::PreparedLoad p = waitForFutureWithEventLoop(future);
  const bool cancelled = token && token->load(std::memory_order_acquire);
  if (cancelled) {
    logViewerLoadResult(QStringLiteral("Image"), displayPath, false, true);
    return false;
  }
  if (!p.ok) {
    logViewerLoadResult(QStringLiteral("Image"), displayPath, false, false);
    return false;
  }

  m_imageView->applyPreparedLoad(p);
  m_stack->setCurrentWidget(m_imageView);
  setFocusProxy(m_imageView);
  m_currentFilePath = displayPath;
  emit fileOpened(displayPath);
  emit viewerStatusChanged(displayPath, m_imageView->statusInfo());
  logViewerLoadResult(QStringLiteral("Image"), displayPath, true, false);
  return true;
}

bool ViewerPanel::openBinaryFile(const QString& filePath, const QString& displayPath) {
  auto token = m_currentCancelToken;
  auto future = QtConcurrent::run(&BinaryView::prepareLoad,
                                   filePath,
                                   m_binaryView->currentUnit(),
                                   m_binaryView->currentEndian(),
                                   m_binaryView->currentEncoding(),
                                   token ? token.get() : nullptr,
                                   /*maxBytes=*/ qint64(-1));
  BinaryView::PreparedLoad p = waitForFutureWithEventLoop(future);
  const bool cancelled = token && token->load(std::memory_order_acquire);
  if (!p.ok) {
    logViewerLoadResult(QStringLiteral("Binary"), displayPath, false, cancelled);
    return false;
  }

  m_binaryView->applyPreparedLoad(p);
  m_stack->setCurrentWidget(m_binaryView);
  setFocusProxy(m_binaryView);
  m_currentFilePath = displayPath;
  emit fileOpened(displayPath);
  emit viewerStatusChanged(displayPath, m_binaryView->statusInfo());
  logViewerLoadResult(QStringLiteral("Binary"), displayPath, true, false);
  return true;
}

bool ViewerPanel::openMarkdownFile(const QString& filePath,
                                    const QString& displayPath) {
  auto token = m_currentCancelToken;
  auto future = QtConcurrent::run(&MarkdownView::prepareLoad,
                                   filePath,
                                   m_markdownView->currentUserEncoding(),
                                   token ? token.get() : nullptr,
                                   /*maxBytes=*/ qint64(-1));
  MarkdownView::PreparedLoad p = waitForFutureWithEventLoop(future);
  const bool cancelled = token && token->load(std::memory_order_acquire);
  if (!p.ok) {
    logViewerLoadResult(QStringLiteral("Markdown"), displayPath, false, cancelled);
    return false;
  }

  m_markdownView->applyPreparedLoad(p);
  m_stack->setCurrentWidget(m_markdownView);
  setFocusProxy(m_markdownView);
  m_currentFilePath = displayPath;
  emit fileOpened(displayPath);
  emit viewerStatusChanged(displayPath, m_markdownView->statusInfo());
  logViewerLoadResult(QStringLiteral("Markdown"), displayPath, true, false);
  return true;
}

bool ViewerPanel::openPdfFile(const QString& filePath,
                               const QString& displayPath) {
  // PDF はワーカーでの prepareLoad は軽い (存在確認のみ)。実ロードは UI 側。
  auto token = m_currentCancelToken;
  auto future = QtConcurrent::run(&PdfView::prepareLoad,
                                   filePath,
                                   token ? token.get() : nullptr);
  PdfView::PreparedLoad p = waitForFutureWithEventLoop(future);
  const bool cancelled = token && token->load(std::memory_order_acquire);
  if (!p.ok) {
    logViewerLoadResult(QStringLiteral("PDF"), displayPath, false, cancelled);
    return false;
  }

  if (!m_pdfView->applyPreparedLoad(p)) {
    // ロード失敗 / 空 PDF は画像と同様に read error 扱い。
    logViewerLoadResult(QStringLiteral("PDF"), displayPath, false, false);
    return false;
  }
  m_stack->setCurrentWidget(m_pdfView);
  setFocusProxy(m_pdfView);
  m_currentFilePath = displayPath;
  emit fileOpened(displayPath);
  emit viewerStatusChanged(displayPath, m_pdfView->statusInfo());
  logViewerLoadResult(QStringLiteral("PDF"), displayPath, true, false);
  return true;
}

bool ViewerPanel::openCsvFile(const QString& filePath,
                               const QString& displayPath) {
  auto token = m_currentCancelToken;
  auto future = QtConcurrent::run(&CsvView::prepareLoad,
                                   filePath,
                                   m_csvView->currentUserEncoding(),
                                   m_csvView->currentDelimiter(),
                                   token ? token.get() : nullptr,
                                   /*maxBytes=*/ qint64(-1));
  CsvView::PreparedLoad p = waitForFutureWithEventLoop(future);
  const bool cancelled = token && token->load(std::memory_order_acquire);
  if (!p.ok) {
    logViewerLoadResult(QStringLiteral("CSV"), displayPath, false, cancelled);
    return false;
  }

  m_csvView->applyPreparedLoad(p);
  m_stack->setCurrentWidget(m_csvView);
  setFocusProxy(m_csvView);
  m_currentFilePath = displayPath;
  emit fileOpened(displayPath);
  emit viewerStatusChanged(displayPath, m_csvView->statusInfo());
  logViewerLoadResult(QStringLiteral("CSV"), displayPath, true, false);
  return true;
}

bool ViewerPanel::openPluginFile(IViewerPlugin* plugin,
                                 const QString& filePath,
                                 const QString& displayPath) {
  if (!plugin) return false;
  clearPluginView();

  // タイトルバー用のローカライズ名 (media 等の同梱プラグイン / 外部プラグイン)。
  m_currentViewerName = localizedViewerName(plugin->pluginId(), plugin->pluginName());

  QWidget* view = plugin->createViewer(
    filePath,
    m_stack,
    ViewerDispatcher::instance().pluginContext());
  if (!view) {
    logViewerLoadResult(QStringLiteral("Plugin:%1").arg(plugin->pluginId()),
                        displayPath, false, false);
    return false;
  }

  // 外部プラグインの createViewer は埋め込み可能な QWidget を返す契約。
  // 念のためトップレベル指定を外して、ViewerPanel のスタック内で管理する。
  view->setWindowFlag(Qt::Window, false);
  view->setAttribute(Qt::WA_DeleteOnClose, false);
  m_pluginView = view;
  m_pluginViewPlugin = plugin;
  if (m_stack->indexOf(view) < 0) {
    m_stack->addWidget(view);
  }
  m_stack->setCurrentWidget(view);
  setFocusProxy(view);
  view->setFocus(Qt::OtherFocusReason);
  // 本体キーバインド設定のショートカット割り当てを、取得 API 経由で push する
  // （media / 外部プラグイン。設定可能項目を持たない外部では何もしない）。
  pushViewerShortcutBindings(view, plugin);
  m_currentFilePath = displayPath;
  emit fileOpened(displayPath);
  // プラグインのビューが statusInfoChanged(QString) シグナルと statusInfo() を
  // 持つ場合 (media 等) は、本体ステータスバーにその要約を出して追従更新する。
  // プラグインは別ターゲットのため、型に依存せずメタオブジェクト経由で扱う。
  const QMetaObject* mo = view->metaObject();
  if (mo->indexOfSignal("statusInfoChanged(QString)") >= 0 &&
      mo->indexOfMethod("statusInfo()") >= 0) {
    QString info;
    QMetaObject::invokeMethod(view, "statusInfo", Qt::DirectConnection,
                              Q_RETURN_ARG(QString, info));
    emit viewerStatusChanged(displayPath, info);
    connect(view, SIGNAL(statusInfoChanged(QString)),
            this, SLOT(onPluginStatusInfoChanged(QString)));
  } else {
    emit viewerStatusChanged(displayPath, plugin->pluginName());
  }
  logViewerLoadResult(QStringLiteral("Plugin:%1").arg(plugin->pluginId()),
                      displayPath, true, false);
  return true;
}

void ViewerPanel::onPluginStatusInfoChanged(const QString& info) {
  emit viewerStatusChanged(m_currentFilePath, info);
}

void ViewerPanel::clear() {
  clearPluginView();
  m_textView->clearContent();
  m_imageView->clearContent();
  m_binaryView->clearContent();
  if (m_markdownView) m_markdownView->clearContent();
  if (m_pdfView) m_pdfView->clearContent();
  if (m_csvView) m_csvView->clearContent();
  m_currentFilePath.clear();
  m_currentViewerName.clear();
  emit fileClosed();
  emit viewerStatusChanged(QString(), QString());
}

} // namespace Farman
