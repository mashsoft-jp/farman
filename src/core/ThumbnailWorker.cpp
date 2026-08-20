#include "ThumbnailWorker.h"
#include "core/ArchiveEntryName.h"
#include <QFileInfo>
#include "utils/ArchivePath.h"

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QImageReader>
#include <QPixmap>

#include <archive.h>
#include <archive_entry.h>

namespace Farman {

namespace {

// アーカイブから 1 つの entry を取り出して画像化する。失敗時は null QImage。
// 同期ブロッキング (worker thread 内で呼ばれる)。entry が大きい場合は丸ごと
// メモリに読み込むので、アーカイブ内 4K 写真などで一時的に数 MB 使う。
QImage loadImageFromArchive(const QString& archivePath, const QString& innerPath) {
  struct archive* src = archive_read_new();
  archive_read_support_format_all(src);
  archive_read_support_filter_all(src);

#ifdef Q_OS_WIN
  // Windows でファイル名にマルチバイト文字が混ざるとき archive_read_open_filename
  // (ANSI 解釈) で失敗するので _w 版を使う。ArchiveExtractWorker と同じパターン。
  const std::wstring w = archivePath.toStdWString();
  const int openResult = archive_read_open_filename_w(src, w.c_str(), 64 * 1024);
#else
  const int openResult = archive_read_open_filename(
    src, archivePath.toUtf8().constData(), 64 * 1024);
#endif
  if (openResult != ARCHIVE_OK) {
    archive_read_free(src);
    return {};
  }

  // innerPath は ArchivePath で先頭 "/" 必須。アーカイブ entry 名は通常 "/" 無し
  // (Zip 形式) なので比較時は先頭 "/" を除いた形に揃える。
  const QString nameEncoding =
    filenameEncodingFor(QFileInfo(archivePath).fileName());

  QString needle = innerPath;
  if (needle.startsWith(QLatin1Char('/'))) needle = needle.mid(1);

  QImage img;
  struct archive_entry* entry = nullptr;
  while (archive_read_next_header(src, &entry) == ARCHIVE_OK) {
    // 一覧側 (ArchiveContext) と同じ判定で復号し、CP932 zip でも needle と一致
    // させる。末尾/先頭 '/' は付かない前提 (readEntryPath と同様)。
    QString name = decodeArchiveEntryName(entry, nameEncoding);
    while (name.endsWith(QLatin1Char('/'))) name.chop(1);
    while (name.startsWith(QLatin1Char('/'))) name.remove(0, 1);
    if (name != needle) {
      archive_read_data_skip(src);
      continue;
    }
    // 目的の entry を見つけた → データ全体を読み出す
    QByteArray data;
    const qint64 sizeHint = static_cast<qint64>(archive_entry_size(entry));
    if (sizeHint > 0) data.reserve(static_cast<int>(qMin<qint64>(sizeHint, INT_MAX)));
    char buf[16 * 1024];
    while (true) {
      const la_ssize_t n = archive_read_data(src, buf, sizeof(buf));
      if (n < 0) {
        // 読み出し中のエラー (暗号化 entry でパスワード未提供等)。null 返却。
        data.clear();
        break;
      }
      if (n == 0) break;
      data.append(buf, static_cast<int>(n));
    }
    if (!data.isEmpty()) {
      img.loadFromData(data);
    }
    break;
  }

  archive_read_close(src);
  archive_read_free(src);
  return img;
}

} // anonymous namespace

ThumbnailWorker::ThumbnailWorker(std::atomic<quint64>* currentGen, QObject* parent)
  : QObject(parent), m_currentGen(currentGen) {}

void ThumbnailWorker::process(ThumbnailKey key, quint64 requestGen) {
  // 古いリクエストは早期スキップ。decode 前にカウンタを確認することで、
  // ディレクトリ移動連打時のキュー詰まりを避ける。
  if (m_currentGen && m_currentGen->load() != requestGen) {
    emit done(key, requestGen, QPixmap());  // 受信側で null は無視される
    return;
  }

  const int target = key.sizePx;
  QImage img;

  // パスがアーカイブ内エントリ ("archive.zip!/inner/path") かを判定。
  const ArchivePath::Split split = ArchivePath::splitArchivePath(key.path);
  if (split.valid) {
    // アーカイブ内: libarchive で entry を抽出してメモリ image にする。
    // QImageReader の setScaledSize は使えない (ファイルパス入力ではない) ので
    // 後段で QImage::scaled を呼んで target 内に収める。
    img = loadImageFromArchive(split.archivePath, split.innerPath);
  } else {
    // 通常 FS: QImageReader 経由で scale ヒント付き decode。
    QImageReader reader(key.path);
    reader.setAutoTransform(true);
    const QSize src = reader.size();
    if (src.isValid() && !src.isEmpty()) {
      const QSize scaled = src.scaled(target, target, Qt::KeepAspectRatio);
      if (!scaled.isEmpty()) reader.setScaledSize(scaled);
    }
    img = reader.read();
  }

  if (img.isNull()) {
    emit done(key, requestGen, QPixmap());
    return;
  }
  // setScaledSize は decoder ヒントで target を超える結果が返ることがある
  // (SVG / libjpeg-turbo の coarse scale)。アーカイブ抽出経路はそもそも
  // ヒント無しでフルサイズ decode するため必ず scale が必要。最終 scale で
  // target 内に収める。
  if (img.width() > target || img.height() > target) {
    img = img.scaled(target, target, Qt::KeepAspectRatio,
                     Qt::SmoothTransformation);
  }
  emit done(key, requestGen, QPixmap::fromImage(std::move(img)));
}

} // namespace Farman
