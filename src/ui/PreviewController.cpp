#include "PreviewController.h"

#include "PreviewPane.h"
#include "core/ArchiveContext.h"
#include "core/Logger.h"
#include "core/workers/PropertiesWorker.h"
#include "settings/Settings.h"
#include "ui/ViewerPanel.h"
#include "utils/ArchivePath.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QTemporaryDir>
#include <QtConcurrent/QtConcurrentRun>

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
  enum class Kind { Text, Image, Binary, Markdown, Pdf, Csv, Unsupported };
  Kind kind = Kind::Unsupported;
  QString errorReason;  // Unsupported のときに表示するメッセージ
  TextView::PreparedLoad     text;
  ImageView::PreparedLoad    image;
  BinaryView::PreparedLoad   binary;
  MarkdownView::PreparedLoad markdown;
  PdfView::PreparedLoad      pdf;
  CsvView::PreparedLoad      csv;
};

// ワーカースレッドで走る本体。UI には触らないので static の自由関数にする。
// cancelToken は shared_ptr で受けて lifetime を確実にする (PreviewController が
// 破棄されてもワーカー実行中は生存)。
// maxBytes > 0 のときはテキスト/バイナリの読み込みを先頭 maxBytes に絞り、
// truncate 注記付きで返す (プレビューモードの上限処理用)。
PreviewResult runPreviewLoad(QString             filePath,
                             QString             userEncoding,
                             BinaryViewerUnit    binUnit,
                             BinaryViewerEndian  binEndian,
                             QString             binEncoding,
                             std::shared_ptr<std::atomic<bool>> cancelToken,
                             qint64              maxBytes) {
  PreviewResult r;
  const std::atomic<bool>* tokPtr = cancelToken.get();
  // 早期キャンセル: ジョブが投入から実行までの間にキャンセルされていたら何もしない。
  if (tokPtr && tokPtr->load(std::memory_order_acquire)) {
    r.kind = PreviewResult::Kind::Unsupported;
    return r;
  }
  const ViewerPanel::ViewerKind kind = ViewerPanel::resolveAuto(filePath);
  switch (kind) {
    case ViewerPanel::ViewerKind::Text:
      r.text = TextView::prepareLoad(filePath, userEncoding, tokPtr, maxBytes);
      r.kind = PreviewResult::Kind::Text;
      if (!r.text.ok) {
        r.kind = PreviewResult::Kind::Unsupported;
        r.errorReason = QObject::tr("Failed to load text.");
      }
      return r;
    case ViewerPanel::ViewerKind::Image:
      // ImageView::prepareLoad は QImageReader::read() の最中に中断不可。
      // 開始前にだけキャンセルチェックを掛ける。画像は途中切り取りができない
      // (デコーダがファイル末尾までを要求する) ので、maxBytes は無視する。
      r.image = ImageView::prepareLoad(filePath);
      r.kind  = PreviewResult::Kind::Image;
      if (!r.image.ok) {
        r.kind = PreviewResult::Kind::Unsupported;
        r.errorReason = QObject::tr("Failed to load image.");
      }
      return r;
    case ViewerPanel::ViewerKind::Binary:
      r.binary = BinaryView::prepareLoad(filePath, binUnit, binEndian,
                                          binEncoding, tokPtr, maxBytes);
      r.kind   = PreviewResult::Kind::Binary;
      if (!r.binary.ok) {
        r.kind = PreviewResult::Kind::Unsupported;
        r.errorReason = QObject::tr("Failed to load file.");
      }
      return r;
    case ViewerPanel::ViewerKind::Markdown:
      r.markdown = MarkdownView::prepareLoad(filePath, userEncoding,
                                              tokPtr, maxBytes);
      r.kind     = PreviewResult::Kind::Markdown;
      if (!r.markdown.ok) {
        r.kind = PreviewResult::Kind::Unsupported;
        r.errorReason = QObject::tr("Failed to load Markdown.");
      }
      return r;
    case ViewerPanel::ViewerKind::Pdf:
      r.pdf  = PdfView::prepareLoad(filePath, tokPtr);
      r.kind = PreviewResult::Kind::Pdf;
      if (!r.pdf.ok) {
        r.kind = PreviewResult::Kind::Unsupported;
        r.errorReason = QObject::tr("Failed to load PDF.");
      }
      return r;
    case ViewerPanel::ViewerKind::Csv:
      r.csv  = CsvView::prepareLoad(filePath, userEncoding,
                                     CsvView::Delimiter::Auto, tokPtr, maxBytes);
      r.kind = PreviewResult::Kind::Csv;
      if (!r.csv.ok) {
        r.kind = PreviewResult::Kind::Unsupported;
        r.errorReason = QObject::tr("Failed to load CSV.");
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

PreviewController::~PreviewController() {
  // 現在 m_dirSizeWorker に紐付いている worker は cancel + wait + delete を
  // 同期実行する。clear 済 (cancelDirSizeWorker で detach された) worker は
  // 自己破棄に任せる (QThread::finished → deleteLater で接続済)。
  // QThread を未停止のまま破棄すると Qt::~QThread() が qFatal する。
  if (m_dirSizeWorker) {
    m_dirSizeWorker->requestCancel();
    m_dirSizeWorker->wait();
    delete m_dirSizeWorker.data();
    m_dirSizeWorker.clear();
  }
}

void PreviewController::cancelDirSizeWorker() {
  // 世代を 1 進めて、走り遅れて到着する statsUpdated / finished を捨てる。
  m_dirSizeGeneration.fetch_add(1, std::memory_order_acq_rel);
  if (m_dirSizeWorker) {
    PropertiesWorker* w = m_dirSizeWorker.data();
    m_dirSizeWorker.clear();

    // PreviewController 経由の slot 接続を全部切る (m_pane 越しに UI を
    // 触ってしまうのを防ぐ)。これでこのコントローラからは worker への
    // ハンドルが完全に切り離される。
    w->disconnect(this);

    // **重要**: `deleteLater()` を即時呼ぶと、worker のスレッドが lstat 等で
    // ブロックしているうちに ~QThread() が走り「実行中スレッドの破棄」で
    // qFatal してアプリがクラッシュする (例: Google Drive のような遅い FS で
    // ディレクトリにカーソルを当てた直後に別のディレクトリへ移ったケース)。
    //
    // 代わりに `QThread::finished` (= run() が戻った後に Qt が発火する内蔵
    // シグナル) を receiver 自身の deleteLater に繋ぐ標準イディオムを使う。
    // requestCancel 前に接続するのは、worker が "瞬殺" される極短ケースで
    // finished を取り逃さないようにするため。
    QObject::connect(w, &QThread::finished, w, &QObject::deleteLater);
    w->requestCancel();
  }
}

void PreviewController::requestPreview(const QString& filePath,
                                       const QString& displayPath,
                                       bool           isDirectory,
                                       bool           isDotDot,
                                       qint64         fileSize) {
  // 世代を 1 進めて、現在進行中のジョブが帰ってきても捨てるようにする。
  // (= 「読込み途中でカーソルが移動した場合は読込みを中断して次へ」要件)
  m_generation.fetch_add(1, std::memory_order_acq_rel);

  // 直前のジョブにキャンセルを通知する (Phase 3: 真の中断)。
  if (m_currentCancelToken) {
    m_currentCancelToken->store(true, std::memory_order_release);
  }
  // ディレクトリ集計ワーカも一緒に止める (前のカーソルが大きなディレクトリで
  // 走査中だった場合、放っておくと CPU を浪費する)。
  cancelDirSizeWorker();

  m_pendingFilePath    = filePath;
  m_pendingDisplayPath = displayPath.isEmpty() ? filePath : displayPath;
  m_pendingIsDirectory = isDirectory;
  m_pendingIsDotDot    = isDotDot;
  m_pendingFileSize    = fileSize;
  m_pendingArchiveCtx       = nullptr;       // 通常ファイル経路にリセット
  m_pendingArchiveEntryPath.clear();
  m_hasPending         = true;

  m_debounceTimer.setInterval(Settings::instance().previewDebounceMs());
  m_debounceTimer.start();
}

void PreviewController::requestArchivePreview(const ArchiveContext* ctx,
                                              const QString&        entryPath,
                                              const QString&        displayPath,
                                              qint64                uncompressedSize) {
  m_generation.fetch_add(1, std::memory_order_acq_rel);
  if (m_currentCancelToken) {
    m_currentCancelToken->store(true, std::memory_order_release);
  }
  cancelDirSizeWorker();

  m_pendingFilePath    .clear();   // 展開先パスは onDebounceTimeout で決める
  m_pendingDisplayPath = displayPath.isEmpty() ? entryPath : displayPath;
  m_pendingIsDirectory = false;
  m_pendingIsDotDot    = false;
  m_pendingFileSize    = uncompressedSize;
  m_pendingArchiveCtx  = ctx;
  m_pendingArchiveEntryPath = entryPath;
  m_hasPending         = true;

  m_debounceTimer.setInterval(Settings::instance().previewDebounceMs());
  m_debounceTimer.start();
}

void PreviewController::clearPreview() {
  m_debounceTimer.stop();
  m_hasPending = false;
  m_generation.fetch_add(1, std::memory_order_acq_rel);
  if (m_currentCancelToken) {
    m_currentCancelToken->store(true, std::memory_order_release);
  }
  cancelDirSizeWorker();
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

  // 2. ディレクトリは Finder Quick Look 風の "アイコン + パス + 件数 + 再帰サイズ" 表示。
  //    件数は浅い (再帰しない) entryList で取り、再帰サイズは PropertiesWorker
  //    をバックグラウンドで走らせて随時更新する。.. / . は除外。アクセス権が
  //    無くて読めないディレクトリは -1 で件数行を省略させる。
  if (m_pendingIsDirectory) {
    int itemCount = -1;
    QDir dir(m_pendingFilePath);
    if (dir.exists()) {
      itemCount = dir.entryList(QDir::AllEntries | QDir::Hidden
                                 | QDir::System | QDir::NoDotAndDotDot).size();
    }
    m_pane->showDirectory(m_pendingDisplayPath, itemCount);
    m_lastShownPath = m_pendingFilePath;

    // 再帰サイズ集計をバックグラウンドで開始。前のジョブがまだ走っていれば
    // 中断する。空ディレクトリ / アクセス不可ディレクトリ (itemCount == -1)
    // のときは集計しない (showDirectory で size 行は空表示)。
    cancelDirSizeWorker();
    if (dir.exists() && itemCount > 0) {
      const quint64 gen = ++m_dirSizeGeneration;
      // 親は nullptr。PreviewController 破棄時に Qt の親子削除で worker が
      // 巻き込まれると、走行中スレッドの ~QThread() で qFatal するため。
      // 寿命管理は (a) 現役は m_dirSizeWorker (QPointer) + dtor の wait、
      // (b) detach 済は QThread::finished → deleteLater の標準イディオム
      // ( cancelDirSizeWorker で接続 ) に分けている。
      auto* w = new PropertiesWorker(m_pendingFilePath, nullptr);
      m_dirSizeWorker = w;
      QPointer<PreviewController> self(this);

      // 進捗イベント: statsUpdated は 256 ファイルごとに飛ぶ。
      //   1. その時点の最新値 (bytes, files, dirs) を worker の dynamic
      //      property に書いておく → finished 側で確定表示に使う。
      //   2. PreviewPane を progressive に更新する (finished=false)。
      // PropertiesWorker::run() は最終 statsUpdated を 1 回 emit してから
      // finished(true) を出すので、property は確実に最終値を持つ。
      connect(w, &PropertiesWorker::statsUpdated, this,
              [this, self, gen, w](qint64 bytes, int files, int dirs) {
        if (!self) return;
        if (w) {
          w->setProperty("_lastBytes", QVariant::fromValue(bytes));
          w->setProperty("_lastFiles", files);
          w->setProperty("_lastDirs",  dirs);
        }
        if (gen != m_dirSizeGeneration.load(std::memory_order_acquire)) return;
        if (m_pane) m_pane->showDirectorySize(bytes, files, dirs, /*finished=*/false);
      });

      // 完了イベント: 「集計中…」サフィックスを外して確定表示に切替 + 破棄。
      // 世代不一致 (= 既に次のディレクトリへ移動済) なら表示は触らない。
      connect(w, &PropertiesWorker::finished, this,
              [this, self, gen, w](bool ok) {
        if (self
            && ok
            && gen == m_dirSizeGeneration.load(std::memory_order_acquire)
            && m_pane && w) {
          const qint64 bytes = w->property("_lastBytes").toLongLong();
          const int    files = w->property("_lastFiles").toInt();
          const int    dirs  = w->property("_lastDirs").toInt();
          m_pane->showDirectorySize(bytes, files, dirs, /*finished=*/true);
        }
        if (w) w->deleteLater();
      });
      w->start();
    }
    return;
  }

  // 3. 上限超過: テキスト/バイナリは先頭 N バイトに truncate して表示、
  //    画像は途中切り取りできないので「too large」で拒否。
  //    種別判定は resolveAuto なので、表示用パスではなく実パスで判定する。
  //    アーカイブ内エントリは entryPath の拡張子で判定する。
  const qint64 maxBytes = Settings::instance().previewMaxFileSizeBytes();
  const QString pathForKind = m_pendingArchiveCtx
                                ? m_pendingArchiveEntryPath
                                : m_pendingFilePath;
  if (m_pendingFileSize > maxBytes
      && ViewerPanel::resolveAuto(pathForKind) == ViewerPanel::ViewerKind::Image) {
    m_pane->showUnsupported(tr("File too large to preview (%1).\n"
                               "Press Enter to open in viewer.")
                              .arg(humanReadableSize(m_pendingFileSize)));
    m_lastShownPath = m_pendingDisplayPath;
    return;
  }

  // 4-A. アーカイブ内エントリ: 一時展開してから通常ロード経路へ。
  if (m_pendingArchiveCtx) {
    // 同じエントリの再要求はスキップ (lastShownPath は表示用パスで比較)。
    if (!m_pendingDisplayPath.isEmpty()
        && m_pendingDisplayPath == m_lastShownPath) {
      return;
    }

    // 一時ディレクトリは初回要求時に lazy 生成 (Preview を全く使わない
    // セッションで余計な temp dir を作らないため)。
    if (!m_archiveTempDir) {
      m_archiveTempDir = std::make_unique<QTemporaryDir>(
        QDir::tempPath() + QStringLiteral("/farman-preview-arch-XXXXXX"));
    }
    if (!m_archiveTempDir->isValid()) {
      m_pane->showUnsupported(tr("Failed to prepare temp directory for archive."));
      m_lastShownPath = m_pendingDisplayPath;
      return;
    }

    // 決定論的命名: archive path の SHA1 prefix をサブディレクトリ名にして
    // 衝突回避 + 同じエントリは再展開しない。
    const QByteArray archHash = QCryptographicHash::hash(
      m_pendingArchiveCtx->archivePath.toUtf8(),
      QCryptographicHash::Sha1).toHex().left(8);
    const QString tempRoot = m_archiveTempDir->path()
      + QStringLiteral("/") + QString::fromLatin1(archHash);
    const QString tempPath = ArchivePath::safeJoinExtractPath(
      tempRoot, m_pendingArchiveEntryPath);
    if (tempPath.isEmpty()) {
      m_pane->showUnsupported(tr("Unsafe archive entry path: %1")
                                .arg(m_pendingArchiveEntryPath));
      m_lastShownPath = m_pendingDisplayPath;
      return;
    }

    // 既に同じ展開先が存在し、サイズが合うならそのまま使う (再展開しない)。
    const QFileInfo existing(tempPath);
    if (!existing.exists() || existing.size() != m_pendingFileSize) {
      if (!m_pendingArchiveCtx->extractEntryTo(m_pendingArchiveEntryPath, tempPath)) {
        m_pane->showUnsupported(tr("Failed to extract '%1' from archive.")
                                  .arg(m_pendingArchiveEntryPath));
        m_lastShownPath = m_pendingDisplayPath;
        return;
      }
    }

    // 展開済みパスを使って通常のワーカー経路へ。lastShownPath は表示用パスで
    // 更新するので、同じエントリへの繰返し要求は 3 行上でスキップされる。
    startLoadJob(tempPath, m_pendingDisplayPath);
    m_lastShownPath = m_pendingDisplayPath;
    return;
  }

  // 4-B. 通常ファイル: 同じファイルが連続で要求されたら何もしない。
  if (!m_pendingFilePath.isEmpty() && m_pendingFilePath == m_lastShownPath) {
    return;
  }

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

  startLoadJob(m_pendingFilePath, m_pendingDisplayPath);
}

void PreviewController::startLoadJob(const QString& filePath,
                                     const QString& /*displayPath*/) {
  if (!m_pane) return;

  // ワーカー投入時点での世代をキャプチャ。
  const quint64 myGen = m_generation.load(std::memory_order_acquire);

  // このジョブ用の新しいキャンセルトークンを発行する。
  // 次の requestPreview / clearPreview で *this token = true がセットされ、
  // ワーカーが prepareLoad の中で見て早期 return する仕組み。
  auto token = std::make_shared<std::atomic<bool>>(false);
  m_currentCancelToken = token;

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
  // ジョブ完了時に止められるよう、共有フラグで「完了したか」を伝える。
  auto jobFinished = std::make_shared<std::atomic<bool>>(false);
  QTimer::singleShot(150, this, [this, myGen, jobFinished]() {
    if (jobFinished->load(std::memory_order_acquire)) {
      // ジョブが先に完了していたら Loading は出さない。
      return;
    }
    if (myGen != m_generation.load(std::memory_order_acquire)) {
      // 既により新しい要求が来ている → このジョブの Loading は出さない。
      return;
    }
    if (m_pane) m_pane->showLoading();
  });

  // QtConcurrent::run でワーカースレッドへ。完了時は QFutureWatcher の
  // finished スロットで結果を回収する。
  auto* watcher = new QFutureWatcher<PreviewResult>(this);
  QPointer<PreviewController> self(this);
  connect(watcher, &QFutureWatcher<PreviewResult>::finished,
          this, [this, self, watcher, myGen, jobFinished]() {
    // まず「ジョブ完了」フラグを立てて、150ms Loading 表示の発火を抑止する。
    jobFinished->store(true, std::memory_order_release);
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
      case PreviewResult::Kind::Markdown:
        m_pane->showMarkdown(r.markdown);
        m_lastShownPath = r.markdown.filePath;
        break;
      case PreviewResult::Kind::Pdf:
        m_pane->showPdf(r.pdf);
        m_lastShownPath = r.pdf.filePath;
        break;
      case PreviewResult::Kind::Csv:
        m_pane->showCsv(r.csv);
        m_lastShownPath = r.csv.filePath;
        break;
      case PreviewResult::Kind::Unsupported:
        m_pane->showUnsupported(r.errorReason);
        break;
    }
  });

  // 上限超え (Text/Binary を先頭 truncate で見せる場合) の maxBytes を決定。
  // ファイルサイズが上限以下のときは -1 (フル読み) を渡してフォーマッタの
  // 内蔵上限に任せる。
  const qint64 settingsMax = Settings::instance().previewMaxFileSizeBytes();
  const qint64 maxBytes = (m_pendingFileSize > settingsMax) ? settingsMax : -1;

  watcher->setFuture(QtConcurrent::run(&runPreviewLoad,
                                       filePath,
                                       userEnc,
                                       binUnit,
                                       binEndian,
                                       binEncoding,
                                       token,
                                       maxBytes));
}

} // namespace Farman
