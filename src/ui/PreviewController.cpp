#include "PreviewController.h"

#include "PreviewPane.h"
#include "settings/Settings.h"
#include "ui/ViewerPanel.h"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QVariant>
#include <QtConcurrent/QtConcurrentRun>

#include <variant>

namespace Farman {

namespace {

// バイトを人間可読サイズに変換する小ヘルパ。
QString humanReadableSize(qint64 bytes) {
  static const char* units[] = { "B", "KB", "MB", "GB", "TB" };
  double v = static_cast<double>(bytes);
  int u = 0;
  while (v >= 1024.0 && u + 1 < int(sizeof(units) / sizeof(units[0]))) {
    v /= 1024.0;
    ++u;
  }
  if (u == 0) {
    return QStringLiteral("%1 B").arg(bytes);
  }
  return QStringLiteral("%1 %2").arg(v, 0, 'f', 1).arg(QString::fromLatin1(units[u]));
}

// ワーカーが返す結果の合併型。kind に応じてどの PreparedLoad が
// セットされているかを判別する。
struct PreviewResult {
  enum class Kind { Text, Image, Binary, Unsupported };
  Kind kind = Kind::Unsupported;
  QString errorReason;  // Unsupported のときに表示するメッセージ
  TextView::PreparedLoad   text;
  ImageView::PreparedLoad  image;
  BinaryView::PreparedLoad binary;
};

// ワーカースレッドで走る本体。UI には触らないので static の自由関数にする。
PreviewResult runPreviewLoad(QString             filePath,
                             QString             userEncoding,
                             BinaryViewerUnit    binUnit,
                             BinaryViewerEndian  binEndian,
                             QString             binEncoding) {
  PreviewResult r;
  const ViewerPanel::ViewerKind kind = ViewerPanel::resolveAuto(filePath);
  switch (kind) {
    case ViewerPanel::ViewerKind::Text:
      r.text = TextView::prepareLoad(filePath, userEncoding);
      r.kind = PreviewResult::Kind::Text;
      if (!r.text.ok) {
        r.kind = PreviewResult::Kind::Unsupported;
        r.errorReason = QObject::tr("Failed to load text.");
      }
      return r;
    case ViewerPanel::ViewerKind::Image:
      r.image = ImageView::prepareLoad(filePath);
      r.kind  = PreviewResult::Kind::Image;
      if (!r.image.ok) {
        r.kind = PreviewResult::Kind::Unsupported;
        r.errorReason = QObject::tr("Failed to load image.");
      }
      return r;
    case ViewerPanel::ViewerKind::Binary:
      r.binary = BinaryView::prepareLoad(filePath, binUnit, binEndian, binEncoding);
      r.kind   = PreviewResult::Kind::Binary;
      if (!r.binary.ok) {
        r.kind = PreviewResult::Kind::Unsupported;
        r.errorReason = QObject::tr("Failed to load file.");
      }
      return r;
    case ViewerPanel::ViewerKind::Auto:
    default:
      r.kind = PreviewResult::Kind::Unsupported;
      r.errorReason = QObject::tr("Unsupported file type.");
      return r;
  }
}

} // namespace

PreviewController::PreviewController(PreviewPane* pane, QObject* parent)
  : QObject(parent), m_pane(pane) {
  m_debounceTimer.setSingleShot(true);
  m_debounceTimer.setInterval(Settings::instance().previewDebounceMs());
  connect(&m_debounceTimer, &QTimer::timeout,
          this,             &PreviewController::onDebounceTimeout);
}

void PreviewController::requestPreview(const QString& filePath,
                                       const QString& displayPath,
                                       bool           isDirectory,
                                       bool           isDotDot,
                                       qint64         fileSize) {
  // 世代を 1 進めて、現在進行中のジョブが帰ってきても捨てるようにする。
  // (= 「読込み途中でカーソルが移動した場合は読込みを中断して次へ」要件)
  m_generation.fetch_add(1, std::memory_order_acq_rel);

  m_pendingFilePath    = filePath;
  m_pendingDisplayPath = displayPath.isEmpty() ? filePath : displayPath;
  m_pendingIsDirectory = isDirectory;
  m_pendingIsDotDot    = isDotDot;
  m_pendingFileSize    = fileSize;
  m_hasPending         = true;

  m_debounceTimer.setInterval(Settings::instance().previewDebounceMs());
  m_debounceTimer.start();
}

void PreviewController::clearPreview() {
  m_debounceTimer.stop();
  m_hasPending = false;
  m_generation.fetch_add(1, std::memory_order_acq_rel);
  m_lastShownPath.clear();
  if (m_pane) m_pane->clear();
}

void PreviewController::onDebounceTimeout() {
  if (!m_hasPending || !m_pane) return;
  m_hasPending = false;

  // 1. ".." 行はプレビュー対象にしない。
  if (m_pendingIsDotDot) {
    m_pane->clear();
    m_lastShownPath.clear();
    return;
  }

  // 2. ディレクトリは状態表示。
  if (m_pendingIsDirectory) {
    m_pane->showUnsupported(tr("Directory: %1").arg(m_pendingDisplayPath));
    m_lastShownPath = m_pendingFilePath;
    return;
  }

  // 3. 同じファイルが連続で要求されたら何もしない。
  if (!m_pendingFilePath.isEmpty() && m_pendingFilePath == m_lastShownPath) {
    return;
  }

  // 4. 通常ファイル以外を弾く。
  const QFileInfo fi(m_pendingFilePath);
  if (!fi.exists()) {
    m_pane->showUnsupported(tr("File not found."));
    m_lastShownPath = m_pendingFilePath;
    return;
  }
  if (!fi.isFile()) {
    m_pane->showUnsupported(tr("Not a regular file."));
    m_lastShownPath = m_pendingFilePath;
    return;
  }

  // 5. 上限超過チェック。
  const qint64 maxBytes = Settings::instance().previewMaxFileSizeBytes();
  if (m_pendingFileSize > maxBytes) {
    m_pane->showUnsupported(tr("File too large to preview (%1).\n"
                               "Press Enter to open in viewer.")
                              .arg(humanReadableSize(m_pendingFileSize)));
    m_lastShownPath = m_pendingFilePath;
    return;
  }

  // 6. 通常ファイル → ワーカーへ投げる。
  startLoadJob(m_pendingFilePath, m_pendingDisplayPath);
}

void PreviewController::startLoadJob(const QString& filePath,
                                     const QString& /*displayPath*/) {
  if (!m_pane) return;

  // ワーカー投入時点での世代をキャプチャ。
  const quint64 myGen = m_generation.load(std::memory_order_acquire);

  // ビュアー用のローカル設定をメインスレッドで取得 (UI 触れる)。
  const QString userEnc = m_pane->textView()
                            ? m_pane->textView()->currentUserEncoding()
                            : QString();
  BinaryView* bv = m_pane->binaryView();
  const BinaryViewerUnit   binUnit     = bv ? bv->currentUnit()
                                            : BinaryViewerUnit::Byte1;
  const BinaryViewerEndian binEndian   = bv ? bv->currentEndian()
                                            : BinaryViewerEndian::Little;
  const QString            binEncoding = bv ? bv->currentEncoding()
                                            : QStringLiteral("Auto");

  // 150ms 経っても結果が返ってこないときだけ Loading 表示を出す
  // (短時間で完了する小ファイルでは Loading をチカチカさせない)。
  QTimer::singleShot(150, this, [this, myGen]() {
    if (myGen != m_generation.load(std::memory_order_acquire)) {
      // 既により新しい要求が来ている → このジョブの Loading は出さない
      return;
    }
    if (m_pane) m_pane->showLoading();
  });

  // QtConcurrent::run でワーカースレッドへ。完了時は QFutureWatcher の
  // finished スロットで結果を回収する。
  auto* watcher = new QFutureWatcher<PreviewResult>(this);
  QPointer<PreviewController> self(this);
  connect(watcher, &QFutureWatcher<PreviewResult>::finished,
          this, [this, self, watcher, myGen]() {
    watcher->deleteLater();
    if (!self) return;
    // 世代不一致なら結果を捨てる ("読込み途中でカーソル移動 → 中断" 相当)
    if (myGen != m_generation.load(std::memory_order_acquire)) return;
    if (!m_pane) return;

    const PreviewResult r = watcher->result();
    switch (r.kind) {
      case PreviewResult::Kind::Text:
        m_pane->showText(r.text);
        m_lastShownPath = r.text.filePath;
        break;
      case PreviewResult::Kind::Image:
        m_pane->showImage(r.image);
        m_lastShownPath = r.image.filePath;
        break;
      case PreviewResult::Kind::Binary:
        m_pane->showBinary(r.binary);
        m_lastShownPath = r.binary.filePath;
        break;
      case PreviewResult::Kind::Unsupported:
        m_pane->showUnsupported(r.errorReason);
        break;
    }
  });

  watcher->setFuture(QtConcurrent::run(&runPreviewLoad,
                                       filePath,
                                       userEnc,
                                       binUnit,
                                       binEndian,
                                       binEncoding));
}

} // namespace Farman
