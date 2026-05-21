#include "PreviewController.h"

#include "PreviewPane.h"
#include "settings/Settings.h"
#include "ui/ViewerPanel.h"

#include <QFileInfo>

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

} // namespace

PreviewController::PreviewController(PreviewPane* pane, QObject* parent)
  : QObject(parent), m_pane(pane) {
  m_debounceTimer.setSingleShot(true);
  // 既定 200ms。Settings::previewDebounceMs() を参照して以後の requestPreview で
  // 反映する。
  m_debounceTimer.setInterval(Settings::instance().previewDebounceMs());
  connect(&m_debounceTimer, &QTimer::timeout,
          this,             &PreviewController::onDebounceTimeout);
}

void PreviewController::requestPreview(const QString& filePath,
                                       const QString& displayPath,
                                       bool           isDirectory,
                                       bool           isDotDot,
                                       qint64         fileSize) {
  // 何度呼ばれても、最後の要求だけがタイマ発火後に処理される。
  m_pendingFilePath    = filePath;
  m_pendingDisplayPath = displayPath.isEmpty() ? filePath : displayPath;
  m_pendingIsDirectory = isDirectory;
  m_pendingIsDotDot    = isDotDot;
  m_pendingFileSize    = fileSize;
  m_hasPending         = true;

  // Settings 変更で間隔が変わっている場合に追従する。
  m_debounceTimer.setInterval(Settings::instance().previewDebounceMs());
  m_debounceTimer.start();
}

void PreviewController::clearPreview() {
  m_debounceTimer.stop();
  m_hasPending     = false;
  m_lastShownPath.clear();
  if (m_pane) m_pane->clear();
}

void PreviewController::onDebounceTimeout() {
  if (!m_hasPending || !m_pane) return;
  m_hasPending = false;

  // 1. ".." 行はプレビュー対象にしない (左ペインを親に戻すための擬似行)。
  if (m_pendingIsDotDot) {
    m_pane->clear();
    m_lastShownPath.clear();
    return;
  }

  // 2. ディレクトリは「Directory: <path>」状態表示。
  if (m_pendingIsDirectory) {
    m_pane->showUnsupported(tr("Directory: %1").arg(m_pendingDisplayPath));
    m_lastShownPath = m_pendingFilePath;
    return;
  }

  // 3. 同じファイルを連続で要求された場合は再ロードしない。
  if (!m_pendingFilePath.isEmpty() && m_pendingFilePath == m_lastShownPath) {
    return;
  }

  // 4. 通常ファイル以外 (特殊ファイル / 存在しない) はスキップ。
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

  // 6. 通常ファイル → 種別を判定して prepareLoad → applyPreparedLoad。
  loadAndShow(m_pendingFilePath, m_pendingDisplayPath);
}

void PreviewController::loadAndShow(const QString& filePath,
                                    const QString& /*displayPath*/) {
  if (!m_pane) return;

  // ViewerPanel と同じ判定ロジックで Text / Image / Binary を選ぶ。
  const ViewerPanel::ViewerKind kind = ViewerPanel::resolveAuto(filePath);

  switch (kind) {
    case ViewerPanel::ViewerKind::Text: {
      const QString userEnc = m_pane->textView()
                                ? m_pane->textView()->currentUserEncoding()
                                : QString();
      auto prepared = TextView::prepareLoad(filePath, userEnc);
      if (prepared.ok) {
        m_pane->showText(prepared);
      } else {
        m_pane->showUnsupported(tr("Failed to load text."));
      }
      break;
    }
    case ViewerPanel::ViewerKind::Image: {
      auto prepared = ImageView::prepareLoad(filePath);
      if (prepared.ok) {
        m_pane->showImage(prepared);
      } else {
        m_pane->showUnsupported(tr("Failed to load image."));
      }
      break;
    }
    case ViewerPanel::ViewerKind::Binary: {
      BinaryView* bv = m_pane->binaryView();
      auto prepared  = BinaryView::prepareLoad(
        filePath,
        bv ? bv->currentUnit()     : BinaryViewerUnit::Byte1,
        bv ? bv->currentEndian()   : BinaryViewerEndian::Little,
        bv ? bv->currentEncoding() : QStringLiteral("Auto"));
      if (prepared.ok) {
        m_pane->showBinary(prepared);
      } else {
        m_pane->showUnsupported(tr("Failed to load file."));
      }
      break;
    }
    case ViewerPanel::ViewerKind::Auto:
      // resolveAuto から Auto は戻ってこない想定。
      m_pane->showUnsupported(tr("Unsupported file type."));
      break;
  }

  m_lastShownPath = filePath;
}

} // namespace Farman
