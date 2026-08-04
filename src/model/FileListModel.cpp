#include "FileListModel.h"
#include "settings/Settings.h"
#include "core/ArchiveContext.h"
#include "core/DirectorySizeCache.h"
#include "core/ThumbnailCache.h"
#include "utils/ArchivePath.h"
#include "utils/MediaMatchers.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QInputDialog>
#include <QLineEdit>
#include <QPixmap>
#include "utils/Dialogs.h"
#include <QProgressDialog>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <QEventLoop>
#include <algorithm>
#include <atomic>

namespace Farman {

FileListModel::FileListModel(QObject* parent)
  : QAbstractItemModel(parent) {
  // ThumbnailCache の非同期 decode 結果を受けて、対象 row を再描画する。
  connect(&ThumbnailCache::instance(), &ThumbnailCache::thumbnailReady,
          this, &FileListModel::onThumbnailReady);
  // DirectorySizeCache の非同期算出結果を受けて、対象 row の Size 列を更新する。
  connect(&DirectorySizeCache::instance(), &DirectorySizeCache::sizeReady,
          this, &FileListModel::onDirSizeReady);
}

FileListModel::~FileListModel() = default;

void FileListModel::setActive(bool active) {
  if (m_active == active) return;
  m_active = active;
  if (!m_entries.isEmpty()) {
    emit dataChanged(
      index(0, 0),
      index(m_entries.size() - 1, ColumnCount - 1),
      { Qt::ForegroundRole, Qt::BackgroundRole, Qt::FontRole });
  }
}

void FileListModel::setSinglePaneMode(bool single) {
  if (m_singlePane == single) return;
  m_singlePane = single;
  if (!m_entries.isEmpty()) {
    // サイズ列・更新日時列だけ再描画させる
    emit dataChanged(
      index(0, Size),
      index(m_entries.size() - 1, LastModified),
      { Qt::DisplayRole });
  }
}

void FileListModel::setThumbnailEnabled(bool enabled) {
  if (m_thumbnailEnabled == enabled) return;
  m_thumbnailEnabled = enabled;
  if (!m_entries.isEmpty()) {
    // Name 列の DecorationRole が変わる (実画像 ⇔ 既定アイコン)
    emit dataChanged(
      index(0, Name),
      index(m_entries.size() - 1, Name),
      { Qt::DecorationRole });
  }
}

void FileListModel::setThumbnailPixelSize(int sizePx) {
  if (m_thumbnailPixelSize == sizePx) return;
  m_thumbnailPixelSize = sizePx;
  if (m_thumbnailEnabled && !m_entries.isEmpty()) {
    emit dataChanged(
      index(0, Name),
      index(m_entries.size() - 1, Name),
      { Qt::DecorationRole });
  }
}

void FileListModel::setShowFileIcons(bool show) {
  if (m_showFileIcons == show) return;
  m_showFileIcons = show;
  if (!m_entries.isEmpty()) {
    emit dataChanged(
      index(0, Name),
      index(m_entries.size() - 1, Name),
      { Qt::DecorationRole });
  }
}

QString FileListModel::formatSizeText(qint64 bytes) const {
  const Settings& s = Settings::instance();
  const FileSizeFormat fmt = m_singlePane ? s.fileSizeFormatSingle()
                                          : s.fileSizeFormatDual();
  // 桁区切りカンマは Auto 以外で適用 (デフォルト ON)。
  const bool sep = m_singlePane ? s.fileSizeThousandsSeparatorSingle()
                                : s.fileSizeThousandsSeparatorDual();
  const bool useSep = (fmt != FileSizeFormat::Auto) && sep;
  QLocale loc(QLocale::English);
  if (!useSep) {
    // ロケールの数値書式から 1000 区切りを外す
    loc.setNumberOptions(QLocale::OmitGroupSeparator);
  }
  switch (fmt) {
    case FileSizeFormat::Bytes:
      // 素のバイト数。OmitGroupSeparator を切替えるだけで
      // "12,345,678" と "12345678" が切替わる。
      return QStringLiteral("%1 B").arg(loc.toString(bytes));
    case FileSizeFormat::SI:
      // 1000-based KB/MB/GB
      return loc.formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
    case FileSizeFormat::IEC:
      // 1024-based KiB/MiB/GiB
      return loc.formattedDataSize(bytes, 1, QLocale::DataSizeIecFormat);
    case FileSizeFormat::Auto:
    default:
      // Auto は桁区切りトグルを無視し、ロケール既定に任せる
      // (英語ロケールで 1024-based + KB ラベル)。
      return QLocale(QLocale::English).formattedDataSize(bytes);
  }
}

qint64 FileListModel::effectiveSizeForSort(const FileItem* item) const {
  if (m_computeDirSizes && item->isDir() && !item->isDotDot()) {
    const auto it = m_dirSizes.constFind(item->absolutePath());
    if (it != m_dirSizes.cend() && it.value() >= 0) {
      return it.value();
    }
    return 0;  // 未算出は 0 扱い
  }
  return item->size();
}

void FileListModel::setComputeDirSizes(bool on) {
  if (m_computeDirSizes == on) return;
  m_computeDirSizes = on;
  if (on) {
    // 有効化: 現在の一覧について算出を開始する。
    startDirSizeComputation();
  } else {
    // 無効化: 表示を従来の "<DIR>" に戻す。
    m_dirSizes.clear();
  }
  // Size / Type 両列の表示が変わるので再描画させる。
  if (!m_entries.isEmpty()) {
    emit dataChanged(index(0, Type),
                     index(m_entries.size() - 1, Size),
                     { Qt::DisplayRole });
  }
}

void FileListModel::startDirSizeComputation() {
  if (!m_computeDirSizes) return;
  // アーカイブ内はローカル FS の再帰集計ができないので対象外。
  if (m_archiveContext) return;

  DirectorySizeCache& cache = DirectorySizeCache::instance();
  // NOTE: ここでは generation を bump しない。左右ペインが同一の
  // DirectorySizeCache (単一 worker) を共有しており、bump するともう一方の
  // ペインの算出中ジョブまで打ち切ってしまうため (後から読み込んだペインが
  // 先のペインの計算をキャンセルしてしまう)。ディレクトリ移動で不要になった
  // 結果は onDirSizeReady 側で「現ディレクトリに無い path は無視」することで
  // 実害なく捨てられる。走査中ジョブの中断はアプリ終了時のみ (デストラクタ)。
  m_dirSizes.clear();

  const Settings& s = Settings::instance();
  const bool cached = (s.directorySizeCacheMode() == DirSizeCacheMode::Cached);
  // Cached: 保持秒数を ms に。Always: 経過時間を無視 (常にキャッシュ未使用)。
  const qint64 maxAgeMs =
    cached ? static_cast<qint64>(s.directorySizeCacheSeconds()) * 1000 : 0;

  for (const auto& sp : m_entries) {
    const FileItem* it = sp.get();
    if (!it->isDir() || it->isDotDot()) continue;
    const QString path = it->absolutePath();
    if (path.isEmpty()) continue;

    qint64 bytes = 0;
    bool hit = false;
    if (cached) {
      // mtime も無効化条件に併用する (ディレクトリ直下の増減に追従)。
      const qint64 mtimeMs = it->lastModified().isValid()
                               ? it->lastModified().toMSecsSinceEpoch()
                               : -1;
      hit = cache.peek(path, maxAgeMs, mtimeMs, &bytes);
    }
    if (hit) {
      m_dirSizes.insert(path, bytes);
    } else {
      m_dirSizes.insert(path, -1);  // 算出中プレースホルダ
      cache.request(path);
    }
  }
}

void FileListModel::recomputeDirSizes() {
  if (!m_computeDirSizes) return;
  if (m_archiveContext) return;

  DirectorySizeCache& cache = DirectorySizeCache::instance();
  // 現在の一覧のディレクトリのキャッシュを無効化してから再算出する。
  for (const auto& sp : m_entries) {
    const FileItem* it = sp.get();
    if (it->isDir() && !it->isDotDot()) {
      cache.invalidate(it->absolutePath());
    }
  }
  startDirSizeComputation();
  // プレースホルダ表示に戻す (再算出完了で順次埋まる)。
  if (!m_entries.isEmpty()) {
    emit dataChanged(index(0, Size),
                     index(m_entries.size() - 1, Size),
                     { Qt::DisplayRole });
  }
}

void FileListModel::onDirSizeReady(const QString& path, qint64 totalBytes) {
  auto it = m_dirSizes.find(path);
  if (it == m_dirSizes.end()) return;  // 現ディレクトリ外 (移動後に届いた古い結果)
  it.value() = totalBytes;
  // 該当 row の Size 列を再描画する (Type は "<DIR>" のまま変わらない)。
  for (int r = 0; r < m_entries.size(); ++r) {
    const FileItem* e = m_entries.at(r).get();
    if (e && e->isDir() && e->absolutePath() == path) {
      const QModelIndex idx = index(r, Size);
      emit dataChanged(idx, idx, { Qt::DisplayRole });
      break;
    }
  }
}

void FileListModel::onThumbnailReady(const ThumbnailKey& key,
                                      const QPixmap& /*pixmap*/) {
  if (!m_thumbnailEnabled) return;
  if (key.sizePx != m_thumbnailPixelSize) return;
  // path が現ディレクトリ内のいずれかの row にあるか線形検索 (rows は O(数百)
  // 程度で問題にならない)。あれば DecorationRole を再描画させる。
  // sizeMatch / pathMatch どちらも問わず、データソースは cache 経由で取り直す
  // ので dataChanged を投げるだけで OK。
  for (int r = 0; r < m_entries.size(); ++r) {
    const FileItem* it = m_entries.at(r).get();
    if (it && it->absolutePath() == key.path) {
      const QModelIndex idx = index(r, Name);
      emit dataChanged(idx, idx, { Qt::DecorationRole });
      break;
    }
  }
}

QString FileListModel::currentPath() const {
  return m_currentPath;
}

QString FileListModel::archivePath() const {
  return m_archiveContext ? m_archiveContext->archivePath : QString();
}

namespace {

// アーカイブをワーカースレッドで開き、メインスレッドのイベントループを
// 回し続けながら結果を待つ。ViewerPanel::openFile と同じパターン。
//
// 巨大アーカイブ対応: 500ms 超のロードでは indeterminate QProgressDialog が
// 自動表示され、ユーザーは Cancel ボタンで打ち切れる。 ArchiveContext::load
// は cancelFlag / entriesRead のポインタを受け取り、各エントリ反復で確認する。
// errorOut にはパスワード付き検出・キャンセル・open 失敗のメッセージが書き込まれる。
std::shared_ptr<ArchiveContext> loadArchiveBlocking(const QString& archivePath,
                                                    QString* errorOut) {
  // ロード中の進捗ダイアログ。setMinimumDuration(500ms) により、500ms 以内に
  // 完了する小さなアーカイブでは表示されず、ちらつきが起きない。
  QProgressDialog progress(
    QObject::tr("Reading archive: %1").arg(QFileInfo(archivePath).fileName()),
    QObject::tr("Cancel"),
    0, 0,
    QApplication::activeWindow());
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(500);

  std::atomic<bool> cancelFlag{false};
  std::atomic<int>  entriesRead{0};
  QObject::connect(&progress, &QProgressDialog::canceled,
                   [&cancelFlag]() { cancelFlag.store(true); });

  // 100ms ごとに「N entries read」表示を更新するタイマー (indeterminate でも
  // 数字が動くと「進んでいる」のが分かる)。
  QTimer labelTimer;
  labelTimer.setInterval(100);
  QObject::connect(&labelTimer, &QTimer::timeout, &progress, [&]() {
    const int n = entriesRead.load(std::memory_order_relaxed);
    progress.setLabelText(QObject::tr("Reading archive: %1 (%2 entries)")
      .arg(QFileInfo(archivePath).fileName()).arg(n));
  });
  labelTimer.start();

  auto future = QtConcurrent::run(&ArchiveContext::load,
    archivePath, errorOut, &cancelFlag, &entriesRead);
  QFutureWatcher<std::shared_ptr<ArchiveContext>> watcher;
  QEventLoop loop;
  QObject::connect(&watcher, &QFutureWatcher<std::shared_ptr<ArchiveContext>>::finished,
                   &loop, &QEventLoop::quit);
  watcher.setFuture(future);
  if (!future.isFinished()) {
    // ExcludeUserInputEvents だと QProgressDialog の Cancel ボタンクリックも
    // 含めて全ユーザー入力が読まれないため、巨大アーカイブを止められない。
    // 進捗ダイアログ自身は WindowModal なので親ウィンドウは入力を受け取らず、
    // ユーザーは実質「Cancel を押す」以外の操作はできない。
    loop.exec();
  }
  labelTimer.stop();
  progress.close();
  return watcher.result();
}

} // namespace

bool FileListModel::setPath(const QString& path) {
  m_lastLoadError.clear();
  // ディレクトリ移動でキュー上の古いサムネイル要求を破棄させる
  // (cache 自体は残すので、戻ったときに再利用できる)。
  ThumbnailCache::instance().bumpGeneration();
  // ── アーカイブ内パスの判定 (`!` 区切り) ──────────
  const auto split = ArchivePath::splitArchivePath(path);
  if (split.valid) {
    // アーカイブモードに入る (or 中で移動)
    std::shared_ptr<ArchiveContext> ctx = m_archiveContext;
    if (!ctx || ctx->archivePath != split.archivePath) {
      // 同じアーカイブを既に開いていなければ新規ロード
      QString loadErr;
      ctx = loadArchiveBlocking(split.archivePath, &loadErr);
      if (!ctx) {
        m_lastLoadError = loadErr.isEmpty()
          ? tr("Failed to open archive: %1").arg(split.archivePath)
          : loadErr;
        emit loadFailed(path, m_lastLoadError);
        return false;
      }
      // 暗号化アーカイブの場合はパスワードを入力させて検証する。
      // 取得後 ctx->password に保存し、以後 extractEntryTo /
      // ArchiveExtractEntriesWorker でそれを使う。Cancel された場合は
      // load 失敗扱いで通常 FS に戻る。
      if (ctx->hasEncryptedEntries && ctx->password.isEmpty()) {
        QWidget* parent = QApplication::activeWindow();
        const QString archiveName = QFileInfo(split.archivePath).fileName();
        QString prompt = tr("Enter password for %1:").arg(archiveName);
        for (int attempt = 0; attempt < 3; ++attempt) {
          bool ok = false;
          const QString pw = QInputDialog::getText(parent,
            tr("Password Required"),
            prompt,
            QLineEdit::Password,
            QString(),
            &ok);
          if (!ok) {
            m_lastLoadError = tr("Password input cancelled.");
            emit loadFailed(path, m_lastLoadError);
            return false;
          }
          if (ArchiveContext::verifyPassword(split.archivePath, pw)) {
            ctx->password = pw;
            break;
          }
          // 失敗 → 再入力プロンプトに切替
          prompt = tr("Wrong password. Enter password for %1:").arg(archiveName);
          if (attempt == 2) {
            warn(parent,
              tr("Cannot Open Archive"),
              tr("Wrong password (3 attempts). Giving up."));
            m_lastLoadError = tr("Wrong password (gave up after 3 attempts).");
            emit loadFailed(path, m_lastLoadError);
            return false;
          }
        }
      }
    }
    if (!ctx->isValidDirectory(split.innerPath)) {
      m_lastLoadError = tr("Path not found in archive: %1").arg(split.innerPath);
      emit loadFailed(path, m_lastLoadError);
      return false;
    }

    beginResetModel();

    // 同パスへの "refresh" は overlay を保持する (コピー後など)
    const QString newCurrentPath =
      ArchivePath::joinArchivePath(ctx->archivePath, split.innerPath);
    const bool isRefresh = (newCurrentPath == m_currentPath);

    m_archiveContext   = ctx;
    m_archiveInnerPath = split.innerPath;
    m_currentPath      = newCurrentPath;
    m_allEntries.clear();
    m_entries.clear();
    m_iconCache.clear();  // 別ディレクトリ読込でアイコンキャッシュを破棄
    m_dirSizes.clear();   // ディレクトリサイズ算出結果も破棄
    m_liveFilter.clear();
    // 別ディレクトリへの切替で比較モードは自動解除 (design:
    // directory-comparison.md)。同パス refresh ではユーザーが意図的に
    // モードを継続している (copy 後の更新等) ので残す。
    if (!isRefresh) {
      m_compareMode = false;
      m_compareOverlay.clear();
    }

    // ".." はアーカイブ内のサブディレクトリでも、ルートでも必ず先頭に置く。
    // ルートでは「アーカイブを抜けて FS に戻る」ためのもの。
    // 親 entry は持たないので、空のダミー ArchiveEntry を使う。
    {
      ArchiveEntry dotdot;
      dotdot.name          = QStringLiteral("..");
      dotdot.pathInArchive = QStringLiteral("..");  // 表示・遷移ロジック側で特別扱い
      dotdot.isDir         = true;
      m_allEntries.append(std::make_shared<FileItem>(dotdot, ctx));
    }

    // innerPath 直下のエントリを列挙
    const auto children = ctx->childrenOf(split.innerPath);
    for (const ArchiveEntry* e : children) {
      m_allEntries.append(std::make_shared<FileItem>(*e, ctx));
    }

    applyFilterAndSort();
    endResetModel();
    emit pathChanged(m_currentPath);
    return true;
  }

  // ── 通常の FS パス ──────────
  QDir dir(path);
  if (!dir.exists() || !dir.isReadable()) {
    emit loadFailed(path, "Directory does not exist or is not readable");
    return false;
  }

  beginResetModel();

  // 同パスへの "refresh" は overlay を保持する (コピー後など)
  const QString newCurrentPath = dir.absolutePath();
  const bool isRefresh = (newCurrentPath == m_currentPath) && !m_archiveContext;

  // アーカイブモードから抜ける
  m_archiveContext.reset();
  m_archiveInnerPath.clear();

  m_currentPath = newCurrentPath;
  m_allEntries.clear();
  m_entries.clear();
  m_iconCache.clear();  // 別ディレクトリ読込でアイコンキャッシュを破棄
  m_dirSizes.clear();   // ディレクトリサイズ算出結果も破棄
  // 別ディレクトリへの切替で比較モードは自動解除 (design:
  // directory-comparison.md)。同パス refresh では残す (copy/move/delete 完了後
  // の更新で着色を失わないように)。
  if (!isRefresh) {
    m_compareMode = false;
    m_compareOverlay.clear();
  }
  // 即時フィルタはディレクトリを切り替えたタイミングで自動的にクリアする。
  // 「フィルタが残ったまま別ディレクトリに移ると勝手に絞り込まれる」のを避ける。
  // FileListPane 側のフィルタバーも liveFilterChanged シグナルで同期する。
  m_liveFilter.clear();

  // ディレクトリエントリを読み込む。
  // 隠し・システムファイルも一旦全て取得し、表示可否は applyFilterAndSort で制御する。
  QFileInfoList infoList = dir.entryInfoList(
    QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
    QDir::NoSort  // 自前でソート
  );

  // FileItem に変換 (m_allEntries にだけ入れる。m_entries は applyFilterAndSort で構築)
  for (const QFileInfo& info : infoList) {
    m_allEntries.append(std::make_shared<FileItem>(info));
  }

  // ".." エントリを追加（親ディレクトリがある場合）
  if (dir.cdUp()) {
    QFileInfo parentInfo(dir.absolutePath());
    parentInfo = QFileInfo(m_currentPath + "/..");
    m_allEntries.prepend(std::make_shared<FileItem>(parentInfo));
  }

  applyFilterAndSort();

  endResetModel();

  // ディレクトリサイズのバックグラウンド算出 (有効時)。通常 FS パスのみ対象で
  // アーカイブ内は行わない (startDirSizeComputation 内でも二重ガード)。
  startDirSizeComputation();

  emit pathChanged(m_currentPath);
  return true;
}

void FileListModel::refresh() {
  if (!m_currentPath.isEmpty()) {
    setPath(m_currentPath);
  }
}

void FileListModel::setSortSettings(
  SortKey       key,
  Qt::SortOrder order,
  SortKey       key2nd,
  SortDirsType  dirsType,
  bool          dotFirst,
  Qt::CaseSensitivity cs) {

  m_sortKey    = key;
  m_sortOrder  = order;
  m_sortKey2nd = key2nd;
  m_dirsType   = dirsType;
  m_dotFirst   = dotFirst;
  m_cs         = cs;

  if (!m_entries.isEmpty()) {
    beginResetModel();
    applyFilterAndSort();
    endResetModel();
  }
}

void FileListModel::setNameFilters(const QStringList& patterns) {
  m_nameFilters = patterns;
  // m_allEntries が空のときは setPath 待ち。m_allEntries が入っていれば
  // フィルタを適用し直す (m_entries が一時的に空でも OK)。
  if (!m_allEntries.isEmpty()) {
    beginResetModel();
    applyFilterAndSort();
    endResetModel();
  }
}

void FileListModel::setAttrFilter(AttrFilterFlags flags) {
  m_attrFilter = flags;
  if (!m_allEntries.isEmpty()) {
    beginResetModel();
    applyFilterAndSort();
    endResetModel();
  }
}

void FileListModel::toggleHiddenFiles() {
  if (m_attrFilter.testFlag(AttrFilter::ShowHidden)) {
    m_attrFilter.setFlag(AttrFilter::ShowHidden, false);
  } else {
    m_attrFilter.setFlag(AttrFilter::ShowHidden, true);
  }

  if (!m_allEntries.isEmpty()) {
    beginResetModel();
    applyFilterAndSort();
    endResetModel();
  }
}

void FileListModel::setLiveFilter(const QString& text) {
  if (m_liveFilter == text) return;
  m_liveFilter = text;
  if (!m_allEntries.isEmpty()) {
    beginResetModel();
    applyFilterAndSort();
    endResetModel();
  }
}

void FileListModel::setCompareOverlay(const CompareOverlay& overlay) {
  m_compareOverlay = overlay;
  m_compareMode    = true;
  if (!m_entries.isEmpty()) {
    emit dataChanged(
      index(0, 0),
      index(m_entries.size() - 1, ColumnCount - 1),
      { Qt::ForegroundRole, Qt::BackgroundRole });
  }
}

void FileListModel::clearCompareOverlay() {
  if (!m_compareMode && m_compareOverlay.isEmpty()) return;
  m_compareOverlay.clear();
  m_compareMode = false;
  if (!m_entries.isEmpty()) {
    emit dataChanged(
      index(0, 0),
      index(m_entries.size() - 1, ColumnCount - 1),
      { Qt::ForegroundRole, Qt::BackgroundRole });
  }
}

int FileListModel::selectByCompareStatus(DiffStatus s) {
  if (!m_compareMode || m_compareOverlay.isEmpty()) return 0;
  int selected = 0;
  for (auto& entry : m_entries) {
    if (entry->isDotDot()) continue;
    auto it = m_compareOverlay.constFind(entry->name());
    if (it == m_compareOverlay.cend()) continue;
    if (it.value() != s) continue;
    if (!entry->isSelected()) {
      entry->setSelected(true);
      ++selected;
    }
  }
  if (selected > 0) {
    emit dataChanged(
      index(0, 0),
      index(m_entries.size() - 1, ColumnCount - 1));
  }
  return selected;
}

const FileItem* FileListModel::itemAt(const QModelIndex& index) const {
  if (!index.isValid() || index.row() >= m_entries.size()) {
    return nullptr;
  }
  return m_entries[index.row()].get();
}

const FileItem* FileListModel::itemAt(int row) const {
  if (row < 0 || row >= m_entries.size()) {
    return nullptr;
  }
  return m_entries[row].get();
}

int FileListModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;  // フラットリスト
  }
  return m_entries.size();
}

int FileListModel::columnCount(const QModelIndex& parent) const {
  Q_UNUSED(parent);
  return ColumnCount;
}

QList<int> FileListModel::selectedRows() const {
  QList<int> rows;
  for (int i = 0; i < m_entries.size(); ++i) {
    if (m_entries[i]->isSelected()) {
      rows.append(i);
    }
  }
  return rows;
}

QList<const FileItem*> FileListModel::selectedItems() const {
  QList<const FileItem*> items;
  for (const auto& entry : m_entries) {
    if (entry->isSelected()) {
      items.append(entry.get());
    }
  }
  return items;
}

QStringList FileListModel::selectedFilePaths() const {
  QStringList paths;
  for (const auto& entry : m_entries) {
    if (entry->isSelected() && !entry->isDotDot()) {
      paths.append(entry->absolutePath());
    }
  }
  return paths;
}

void FileListModel::setSelected(int row, bool selected) {
  if (row < 0 || row >= m_entries.size()) {
    return;
  }

  m_entries[row]->setSelected(selected);

  QModelIndex idx = index(row, 0);
  emit dataChanged(idx, index(row, ColumnCount - 1));
}

void FileListModel::setSelectedAll(bool selected) {
  for (auto& entry : m_entries) {
    if (!entry->isDotDot()) {  // ".." は選択しない
      entry->setSelected(selected);
    }
  }

  if (!m_entries.isEmpty()) {
    emit dataChanged(
      index(0, 0),
      index(m_entries.size() - 1, ColumnCount - 1)
    );
  }
}

void FileListModel::toggleSelected(int row) {
  if (row < 0 || row >= m_entries.size()) {
    return;
  }

  const FileItem* item = m_entries[row].get();
  if (item->isDotDot()) {
    return;  // ".." は選択しない
  }

  setSelected(row, !item->isSelected());
}

void FileListModel::invertSelection() {
  for (auto& entry : m_entries) {
    if (!entry->isDotDot()) {
      entry->setSelected(!entry->isSelected());
    }
  }

  if (!m_entries.isEmpty()) {
    emit dataChanged(
      index(0, 0),
      index(m_entries.size() - 1, ColumnCount - 1)
    );
  }
}

bool FileListModel::isAllSelected() const {
  for (const auto& entry : m_entries) {
    if (!entry->isDotDot() && !entry->isSelected()) {
      return false;
    }
  }
  return !m_entries.isEmpty();
}

QModelIndex FileListModel::index(int row, int col, const QModelIndex& parent) const {
  if (parent.isValid() || row < 0 || row >= m_entries.size() ||
      col < 0 || col >= ColumnCount) {
    return {};
  }

  return createIndex(row, col);
}

QModelIndex FileListModel::parent(const QModelIndex& index) const {
  Q_UNUSED(index);
  return {};  // フラットリスト
}

QVariant FileListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= m_entries.size()) {
    return {};
  }

  const FileItem* item = m_entries[index.row()].get();

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
      case Name: {
        QString name = item->name();
        // ".." や "." はそのまま表示
        if (name == ".." || name == ".") {
          return name;
        }
        // ディレクトリは拡張子を切り出さず、名前全体を表示する
        // (例: "My.Project" のようなドット入りディレクトリ名でも分割しない)
        if (item->isDir()) {
          return name;
        }
        // ドットファイル（.から始まる）の場合はファイル名全体を表示
        if (name.startsWith('.')) {
          return name;
        }
        // 通常のファイルは拡張子を除いた名前を表示
        QString suffix = item->suffix();
        if (!suffix.isEmpty()) {
          // 拡張子がある場合は除去
          return name.left(name.length() - suffix.length() - 1);  // -1 は "." の分
        }
        return name;
      }
      case Type: {
        // ディレクトリは Type 列を空にする (拡張子相当の判定をしない)。
        // ただしディレクトリサイズ算出が有効なときは、Size 列に合計サイズを
        // 出す代わりに "<DIR>" を Type 列へ移動する (".." (親) も含める)。
        if (item->isDir()) {
          if (m_computeDirSizes) {
            return QString("<DIR>");
          }
          return QString("");
        }
        QString name = item->name();
        // ドットファイル（.から始まる）の場合はTypeを空にする
        if (name.startsWith('.') && name != ".." && name != ".") {
          return QString("");
        }
        // 通常のファイルは拡張子を表示
        return item->suffix().isEmpty() ? QString("") : item->suffix();
      }
      case Size:
        if (item->isDir()) {
          if (m_computeDirSizes) {
            // ".." (親) はサイズを算出しないので Size は空欄 ("<DIR>" は Type 列)。
            if (item->isDotDot()) {
              return QString();
            }
            // 算出済みなら合計サイズ、未算出 (-1) / 要求前は算出中プレースホルダ。
            const auto it = m_dirSizes.constFind(item->absolutePath());
            if (it != m_dirSizes.cend() && it.value() >= 0) {
              return formatSizeText(it.value());
            }
            return QStringLiteral("…");  // 算出中
          }
          // 算出 OFF のときは従来どおり Size 列に "<DIR>"。
          return QString("<DIR>");
        } else {
          return formatSizeText(item->size());
        }
      case LastModified: {
        const Settings& s = Settings::instance();
        const QString fmt = m_singlePane ? s.dateTimeFormatSingle()
                                         : s.dateTimeFormatDual();
        return item->lastModified().toString(
          fmt.isEmpty() ? QStringLiteral("yyyy/MM/dd HH:mm:ss") : fmt);
      }
      case Created: {
        if (item->name() == QStringLiteral("..")) return QString();
        const QFileInfo& fi = item->fileInfo();
        // birthTime は FS が対応していないと invalid を返す。その場合は空表示。
        const QDateTime t = fi.birthTime();
        if (!t.isValid()) return QString();
        const Settings& s = Settings::instance();
        const QString fmt = m_singlePane ? s.dateTimeFormatSingle()
                                         : s.dateTimeFormatDual();
        return t.toString(fmt.isEmpty() ? QStringLiteral("yyyy/MM/dd HH:mm:ss") : fmt);
      }
      case Permissions: {
        if (item->name() == QStringLiteral("..")) return QString();
        // "rwxr-xr-x" 形式に整形。Qt の QFile::Permissions ビットを参照。
        const QFile::Permissions p = item->fileInfo().permissions();
        QString s = QStringLiteral("---------");
        if (p & QFile::ReadOwner)  s[0] = QLatin1Char('r');
        if (p & QFile::WriteOwner) s[1] = QLatin1Char('w');
        if (p & QFile::ExeOwner)   s[2] = QLatin1Char('x');
        if (p & QFile::ReadGroup)  s[3] = QLatin1Char('r');
        if (p & QFile::WriteGroup) s[4] = QLatin1Char('w');
        if (p & QFile::ExeGroup)   s[5] = QLatin1Char('x');
        if (p & QFile::ReadOther)  s[6] = QLatin1Char('r');
        if (p & QFile::WriteOther) s[7] = QLatin1Char('w');
        if (p & QFile::ExeOther)   s[8] = QLatin1Char('x');
        return s;
      }
      case Attributes: {
        if (item->name() == QStringLiteral("..")) return QString();
        // クロスプラットフォームに使える QFileInfo 系のフラグだけで構成。
        // Windows のシステム/アーカイブビット等は Qt API では取れない。
        const QFileInfo& fi = item->fileInfo();
        QString s;
        if (fi.isHidden())    s.append(QLatin1Char('H'));
        if (!fi.isWritable()) s.append(QLatin1Char('R'));
        if (fi.isSymLink())   s.append(QLatin1Char('L'));
        return s.isEmpty() ? QStringLiteral("-") : s;
      }
      case Owner: {
        if (item->name() == QStringLiteral("..")) return QString();
        const QString owner = item->fileInfo().owner();
        return owner.isEmpty() ? QString::number(item->fileInfo().ownerId())
                               : owner;
      }
      case Group: {
        if (item->name() == QStringLiteral("..")) return QString();
        const QString group = item->fileInfo().group();
        return group.isEmpty() ? QString::number(item->fileInfo().groupId())
                               : group;
      }
      case LinkTarget: {
        if (item->name() == QStringLiteral("..")) return QString();
        const QFileInfo& fi = item->fileInfo();
        if (!fi.isSymLink()) return QString();
        return QStringLiteral("→ %1").arg(fi.symLinkTarget());
      }
    }
  }
  else if (role == Qt::DecorationRole) {
    // Name列にファイルアイコンを表示
    if (index.column() == Name) {
      // サムネイル表示モード ON + 画像ファイル ⇒ ThumbnailCache 経由で
      // 実画像のサムネイルを返す。cache hit なら即返す、miss なら placeholder
      // (既定の拡張子別アイコン) を返して非同期 decode を依頼。受信時は
      // thumbnailReady → onThumbnailReady で該当 row の dataChanged を emit
      // して再描画される。アーカイブ内エントリも対応 (ThumbnailWorker が
      // libarchive で entry を抽出して画像化する)。
      if (m_thumbnailEnabled && !item->isDir() && !item->isDotDot()) {
        const QString path = item->absolutePath();
        if (MediaMatchers::isImageFile(path)) {
          // mtime はアーカイブ内のときアーカイブ自身の mtime を使う (entry の
          // 個別 mtime はサムネイル無効化の粒度として細かすぎる + ArchiveEntry
          // からの取得経路が異なる)。アーカイブが更新されたら全 entry の cache
          // も自然に無効化される。
          qint64 mtimeMs = 0;
          if (m_archiveContext) {
            QFileInfo afi(m_archiveContext->archivePath);
            mtimeMs = afi.lastModified().toMSecsSinceEpoch();
          } else {
            mtimeMs = item->fileInfo().lastModified().toMSecsSinceEpoch();
          }
          ThumbnailKey key{ path, mtimeMs, m_thumbnailPixelSize };
          QPixmap pm;
          auto& cache = ThumbnailCache::instance();
          if (cache.peek(key, &pm)) {
            return QIcon(pm);
          }
          cache.request(key);
        }
      }
      // 種別アイコン非表示設定なら、ここでアイコンを返さない (サムネイル表示
      // モードの実画像プレビューは上で既に返しているので影響しない)。
      if (!m_showFileIcons) {
        return QVariant();
      }
      // ファイルアイコンはパス単位でキャッシュして再フェッチを防ぐ (macOS の
      // QFileIconProvider は遅く、カーソル移動の再描画ごとに取り直すと重い)。
      const QString iconKey = item->absolutePath();
      auto          it      = m_iconCache.constFind(iconKey);
      if (it != m_iconCache.constEnd()) {
        return it.value();
      }
      const QIcon icon = m_iconProvider.icon(item->fileInfo());
      m_iconCache.insert(iconKey, icon);
      return icon;
    }
  }
  else if (role == Qt::BackgroundRole) {
    // Selected が最優先。次にディレクトリ比較の Differ/OnlyHere。最後に
    // カテゴリの通常色 (Hidden/Dir/Normal)。
    FileCategory cat = item->isHidden() ? FileCategory::Hidden
                     : item->isDir()    ? FileCategory::Directory
                                        : FileCategory::Normal;
    const bool inactive = !m_active && Settings::instance().useInactivePaneColors();
    if (!item->isSelected() && m_compareMode && !item->isDotDot()) {
      auto it = m_compareOverlay.constFind(item->name());
      if (it != m_compareOverlay.cend() && it.value() != DiffStatus::Same) {
        const QColor bg = (it.value() == DiffStatus::Differ)
          ? Settings::instance().compareDifferBackground()
          : Settings::instance().compareOnlyHereBackground();
        if (bg.isValid()) return bg;
      }
    }
    const QColor bg = Settings::instance().categoryColor(cat, item->isSelected(), inactive).background;
    if (bg.isValid()) return bg;
  }
  else if (role == Qt::ForegroundRole) {
    FileCategory cat = item->isHidden() ? FileCategory::Hidden
                     : item->isDir()    ? FileCategory::Directory
                                        : FileCategory::Normal;
    const bool inactive = !m_active && Settings::instance().useInactivePaneColors();
    if (!item->isSelected() && m_compareMode && !item->isDotDot()) {
      auto it = m_compareOverlay.constFind(item->name());
      if (it != m_compareOverlay.cend() && it.value() != DiffStatus::Same) {
        const QColor fg = (it.value() == DiffStatus::Differ)
          ? Settings::instance().compareDifferForeground()
          : Settings::instance().compareOnlyHereForeground();
        if (fg.isValid()) return fg;
      }
    }
    const QColor fg = Settings::instance().categoryColor(cat, item->isSelected(), inactive).foreground;
    if (fg.isValid()) return fg;
  }
  else if (role == Qt::FontRole) {
    FileCategory cat = item->isHidden() ? FileCategory::Hidden
                     : item->isDir()    ? FileCategory::Directory
                                        : FileCategory::Normal;
    const bool inactive = !m_active && Settings::instance().useInactivePaneColors();
    if (Settings::instance().categoryColor(cat, item->isSelected(), inactive).bold) {
      // ベースにビューに設定したファイルリスト用フォントを使う。
      // ここで QGuiApplication::font() を使うと、ユーザーが Settings で
      // フォントを変えても太字カテゴリ (既定でディレクトリ) だけ反映されない。
      QFont f = Settings::instance().font();
      f.setBold(true);
      return f;
    }
  }
  else if (role == FileItemRole) {
    return QVariant::fromValue(const_cast<FileItem*>(item));
  }
  else if (role == IsSelectedRole) {
    return item->isSelected();
  }
  else if (role == IsDirRole) {
    return item->isDir();
  }
  else if (role == IsHiddenRole) {
    return item->isHidden();
  }

  return {};
}

QVariant FileListModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return {};
  }

  switch (section) {
    case Name:         return tr("Name");
    case Size:         return tr("Size");
    case Type:         return tr("Type");
    case LastModified: return tr("Modified");
    case Created:      return tr("Created");
    case Permissions:  return tr("Permissions");
    case Attributes:   return tr("Attributes");
    case Owner:        return tr("Owner");
    case Group:        return tr("Group");
    case LinkTarget:   return tr("Link Target");
    default:           return {};
  }
}

bool FileListModel::passesFilters(const FileItem* item) const {
  if (!item) return false;
  // ".." は常に保持 (絞り込み中でも親ディレクトリへ戻れるよう)
  if (item->isDotDot()) return true;

  // 隠しファイルフィルタ
  if (!m_attrFilter.testFlag(AttrFilter::ShowHidden) && item->isHidden()) {
    return false;
  }

  // ディレクトリ/ファイルのみフィルタ
  if (m_attrFilter.testFlag(AttrFilter::DirsOnly)  && !item->isDir()) return false;
  if (m_attrFilter.testFlag(AttrFilter::FilesOnly) &&  item->isDir()) return false;

  // 名前フィルタ (glob)。ディレクトリには適用しない (Sort & Filter ダイアログの
  // 既存挙動に揃える)。
  if (!m_nameFilters.isEmpty() && !item->isDir()) {
    bool matches = false;
    for (const QString& pattern : m_nameFilters) {
      if (QDir::match(pattern, item->name())) { matches = true; break; }
    }
    if (!matches) return false;
  }

  // 即時フィルタ (Quick Filter Bar): 部分一致 (case-insensitive)。
  // ディレクトリにも適用する (フォルダ名で絞りたい場面が多いため)。
  // m_nameFilters と違ってこちらは「その場の表示絞り込み」なので、
  // 「親ディレクトリ (..) を残しつつ、それ以外をテキストで絞る」のが意図。
  if (!m_liveFilter.isEmpty()) {
    if (!item->name().contains(m_liveFilter, Qt::CaseInsensitive)) {
      return false;
    }
  }

  return true;
}

void FileListModel::applyFilterAndSort() {
  // m_allEntries (全件) から passesFilters を通過した shared_ptr だけを
  // m_entries に詰め直す。shared_ptr の参照カウントが上がるだけなので
  // 選択状態などはフィルタ切替で消えない。
  m_entries.clear();
  m_entries.reserve(m_allEntries.size());
  for (const auto& item : m_allEntries) {
    if (passesFilters(item.get())) {
      m_entries.append(item);
    }
  }

  if (m_entries.isEmpty()) {
    return;
  }

  // ソート
  std::stable_sort(m_entries.begin(), m_entries.end(),
    [this](const std::shared_ptr<FileItem>& a, const std::shared_ptr<FileItem>& b) {
      // ".." は常に先頭（ソート設定に関わらず）
      if (a->isDotDot()) return true;
      if (b->isDotDot()) return false;

      // ディレクトリのソート位置
      if (m_dirsType == SortDirsType::First) {
        if (a->isDir() && !b->isDir()) return true;
        if (!a->isDir() && b->isDir()) return false;
      } else if (m_dirsType == SortDirsType::Last) {
        if (a->isDir() && !b->isDir()) return false;
        if (!a->isDir() && b->isDir()) return true;
      }

      // ドットで始まるエントリを同一グループ（ディレクトリ／ファイル）内で先頭に。
      // ディレクトリ・ファイル双方に適用。
      if (m_dotFirst) {
        const bool aDot = a->name().startsWith('.');
        const bool bDot = b->name().startsWith('.');
        if (aDot != bDot) return aDot;
      }

      // 主ソートキー
      int cmp = compareItems(a.get(), b.get(), m_sortKey);
      if (cmp != 0) {
        return m_sortOrder == Qt::AscendingOrder ? cmp < 0 : cmp > 0;
      }

      // 第2ソートキー
      if (m_sortKey2nd != SortKey::None) {
        cmp = compareItems(a.get(), b.get(), m_sortKey2nd);
        return m_sortOrder == Qt::AscendingOrder ? cmp < 0 : cmp > 0;
      }

      return false;
    });
}

int FileListModel::compareItems(const FileItem* a, const FileItem* b, SortKey key) const {
  switch (key) {
    case SortKey::None:
      return 0;
    case SortKey::Name: {
      QString nameA = a->name();
      QString nameB = b->name();
      return nameA.compare(nameB, m_cs);
    }
    case SortKey::Size: {
      // ディレクトリサイズ算出が有効なら、算出済みディレクトリは合計サイズで
      // 比較する。未算出は 0 扱い (effectiveSizeForSort)。
      qint64 sizeA = effectiveSizeForSort(a);
      qint64 sizeB = effectiveSizeForSort(b);
      if (sizeA < sizeB) return -1;
      if (sizeA > sizeB) return 1;
      return 0;
    }
    case SortKey::Type: {
      // ディレクトリは Type 列を持たない扱いにするため、ソート時も
      // 空文字として比較する (suffix() を見ない)。
      QString typeA = a->isDir() ? QString() : a->suffix();
      QString typeB = b->isDir() ? QString() : b->suffix();
      return typeA.compare(typeB, m_cs);
    }
    case SortKey::LastModified: {
      QDateTime dtA = a->lastModified();
      QDateTime dtB = b->lastModified();
      if (dtA < dtB) return -1;
      if (dtA > dtB) return 1;
      return 0;
    }
  }
  return 0;
}

} // namespace Farman
