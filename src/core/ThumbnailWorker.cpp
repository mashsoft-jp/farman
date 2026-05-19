#include "ThumbnailWorker.h"

#include <QImage>
#include <QImageReader>
#include <QPixmap>

namespace Farman {

ThumbnailWorker::ThumbnailWorker(std::atomic<quint64>* currentGen, QObject* parent)
  : QObject(parent), m_currentGen(currentGen) {}

void ThumbnailWorker::process(ThumbnailKey key, quint64 requestGen) {
  // 古いリクエストは早期スキップ。decode 前にカウンタを確認することで、
  // ディレクトリ移動連打時のキュー詰まりを避ける。
  if (m_currentGen && m_currentGen->load() != requestGen) {
    emit done(key, requestGen, QPixmap());  // 受信側で null は無視される
    return;
  }

  QImageReader reader(key.path);
  reader.setAutoTransform(true);
  const int target = key.sizePx;
  const QSize src = reader.size();
  if (src.isValid() && !src.isEmpty()) {
    const QSize scaled = src.scaled(target, target, Qt::KeepAspectRatio);
    if (!scaled.isEmpty()) reader.setScaledSize(scaled);
  }
  QImage img = reader.read();
  if (img.isNull()) {
    emit done(key, requestGen, QPixmap());
    return;
  }
  // setScaledSize は decoder ヒントで target を超える結果が返ることがある
  // (SVG / libjpeg-turbo の coarse scale)。最終 scale で target 内に収める。
  if (img.width() > target || img.height() > target) {
    img = img.scaled(target, target, Qt::KeepAspectRatio,
                     Qt::SmoothTransformation);
  }
  emit done(key, requestGen, QPixmap::fromImage(std::move(img)));
}

} // namespace Farman
