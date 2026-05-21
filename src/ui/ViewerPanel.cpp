#include "ViewerPanel.h"
#include "settings/Settings.h"
#include "viewer/BinaryView.h"
#include "viewer/ImageView.h"
#include "viewer/TextView.h"
#include <QApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMimeDatabase>
#include <QMimeType>
#include <QProgressBar>
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

  loadingLayout->addStretch();
  m_stack->addWidget(m_loadingPage);
}

namespace {

// 拡張子パターン (大文字小文字無視) とマッチするか。
//   - `*` / `?` を含む場合はグロブとして解釈 (例: "c*" は "c", "cc", "cpp" などに一致)
//   - `!` プレフィックスは除外パターン (例: "c* !class" は class を除く c 系拡張子)
//   - 除外がマッチすると即座に不一致確定
//   - 通常パターンが 1 つもなければ何もマッチしない
bool extensionMatches(const QStringList& patterns, const QString& extension) {
  auto patternMatches = [&](const QString& p) {
    if (p.contains(QLatin1Char('*')) || p.contains(QLatin1Char('?'))) {
      QRegularExpression re(
        QRegularExpression::wildcardToRegularExpression(p),
        QRegularExpression::CaseInsensitiveOption);
      return re.match(extension).hasMatch();
    }
    return extension.compare(p, Qt::CaseInsensitive) == 0;
  };

  bool anyInclude = false;
  bool included   = false;
  for (const QString& raw : patterns) {
    QString p = raw.trimmed();
    if (p.isEmpty()) continue;
    const bool isExclude = p.startsWith(QLatin1Char('!'));
    if (isExclude) {
      p = p.mid(1).trimmed();
      if (p.isEmpty()) continue;
      if (patternMatches(p)) return false;  // 除外マッチで即不一致
    } else {
      anyInclude = true;
      if (patternMatches(p)) included = true;
    }
  }
  return anyInclude && included;
}

// MIME パターンとマッチするか。末尾 `*` で前方一致、それ以外は完全一致
// または inherits 判定。
bool mimeMatches(const QStringList& patterns, const QMimeType& mime) {
  const QString name = mime.name();
  for (const QString& p : patterns) {
    const QString trimmed = p.trimmed();
    if (trimmed.isEmpty()) continue;
    if (trimmed.endsWith(QLatin1Char('*'))) {
      const QString prefix = trimmed.left(trimmed.size() - 1);
      if (name.startsWith(prefix, Qt::CaseInsensitive)) return true;
    } else {
      if (name.compare(trimmed, Qt::CaseInsensitive) == 0) return true;
      if (mime.inherits(trimmed)) return true;
    }
  }
  return false;
}

} // anonymous namespace

ViewerPanel::ViewerKind ViewerPanel::resolveAuto(const QString& filePath) {
  // ViewerPanel::openFile() の Auto 分岐と同じルーティング。
  // External モード (独立ウィンドウ) からも同じ判定を使えるよう静的に切り出した。
  const QFileInfo fileInfo(filePath);
  const QString extension = fileInfo.suffix().toLower();
  QMimeDatabase mimeDb;
  const QMimeType mime = mimeDb.mimeTypeForFile(filePath);
  const Settings& s = Settings::instance();

  if (extensionMatches(s.imageViewerExtensions(), extension)
      || mimeMatches(s.imageViewerMimePatterns(), mime)) {
    return ViewerKind::Image;
  }
  if (extensionMatches(s.textViewerExtensions(), extension)
      || mimeMatches(s.textViewerMimePatterns(), mime)) {
    return ViewerKind::Text;
  }
  return ViewerKind::Binary;
}

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
    kind = resolveAuto(pathForStatus);
  }

  bool ok = false;
  switch (kind) {
    case ViewerKind::Text:   ok = openTextFile(filePath, pathForStatus);   break;
    case ViewerKind::Image:  ok = openImageFile(filePath, pathForStatus);  break;
    case ViewerKind::Binary: ok = openBinaryFile(filePath, pathForStatus); break;
    case ViewerKind::Auto:   /* unreachable */ break;
  }

  QApplication::restoreOverrideCursor();
  return ok;
}

void ViewerPanel::showLoadingState(const QString& filePath) {
  const QFileInfo fi(filePath);
  m_loadingTitle->setText(tr("Loading..."));
  const QString sizeStr = QLocale(QLocale::English)
                            .formattedDataSize(fi.size());
  // ファイル名 (太字相当) と サイズ・パスを 2 行で。
  m_loadingDetail->setText(QStringLiteral("%1\n%2 — %3")
                             .arg(fi.fileName(), sizeStr, fi.absolutePath()));
  m_stack->setCurrentWidget(m_loadingPage);

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

// 各ビュアーの prepareLoad をワーカースレッドで実行し、戻り値を待つ間
// メインスレッドのイベントループは回り続ける (= プログレスバーが動く /
// 「読み込み中…」が見える)。ExcludeUserInputEvents で待機中のクリックや
// キー入力は無視する (再入を防ぐ)。
namespace {

template <typename T>
T waitForFutureWithEventLoop(QFuture<T> future) {
  QFutureWatcher<T> watcher;
  QEventLoop        loop;
  QObject::connect(&watcher, &QFutureWatcher<T>::finished,
                   &loop,    &QEventLoop::quit);
  watcher.setFuture(future);
  if (!future.isFinished()) {
    loop.exec(QEventLoop::ExcludeUserInputEvents);
  }
  return watcher.result();
}

} // namespace

bool ViewerPanel::openTextFile(const QString& filePath, const QString& displayPath) {
  auto future = QtConcurrent::run(&TextView::prepareLoad,
                                   filePath,
                                   m_textView->currentUserEncoding(),
                                   /*cancelToken=*/ nullptr,
                                   /*maxBytes=*/ qint64(-1));
  TextView::PreparedLoad p = waitForFutureWithEventLoop(future);
  if (!p.ok) return false;

  m_textView->applyPreparedLoad(p);
  m_stack->setCurrentWidget(m_textView);
  setFocusProxy(m_textView);
  m_currentFilePath = displayPath;
  emit fileOpened(displayPath);
  emit viewerStatusChanged(displayPath, m_textView->statusInfo());
  return true;
}

bool ViewerPanel::openImageFile(const QString& filePath, const QString& displayPath) {
  auto future = QtConcurrent::run(&ImageView::prepareLoad, filePath);
  ImageView::PreparedLoad p = waitForFutureWithEventLoop(future);
  if (!p.ok) return false;

  m_imageView->applyPreparedLoad(p);
  m_stack->setCurrentWidget(m_imageView);
  setFocusProxy(m_imageView);
  m_currentFilePath = displayPath;
  emit fileOpened(displayPath);
  emit viewerStatusChanged(displayPath, m_imageView->statusInfo());
  return true;
}

bool ViewerPanel::openBinaryFile(const QString& filePath, const QString& displayPath) {
  auto future = QtConcurrent::run(&BinaryView::prepareLoad,
                                   filePath,
                                   m_binaryView->currentUnit(),
                                   m_binaryView->currentEndian(),
                                   m_binaryView->currentEncoding(),
                                   /*cancelToken=*/ nullptr,
                                   /*maxBytes=*/ qint64(-1));
  BinaryView::PreparedLoad p = waitForFutureWithEventLoop(future);
  if (!p.ok) return false;

  m_binaryView->applyPreparedLoad(p);
  m_stack->setCurrentWidget(m_binaryView);
  setFocusProxy(m_binaryView);
  m_currentFilePath = displayPath;
  emit fileOpened(displayPath);
  emit viewerStatusChanged(displayPath, m_binaryView->statusInfo());
  return true;
}

void ViewerPanel::clear() {
  m_textView->clearContent();
  m_imageView->clearContent();
  m_binaryView->clearContent();
  m_currentFilePath.clear();
  emit fileClosed();
  emit viewerStatusChanged(QString(), QString());
}

} // namespace Farman
