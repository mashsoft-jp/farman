#include "AiReader.h"

#include <QFile>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QSizeF>
#include <QtGlobal>

namespace Farman {

namespace {

// 描画解像度。PDF の 1 pt (= 1/72 inch) を 2 倍 (= 144 dpi 相当) で描き、
// 長辺が kMaxLongSidePx に収まるよう上限を掛ける。
constexpr qreal kScale          = 2.0;
constexpr int   kMaxLongSidePx  = 4096;

void setError(QString* out, const QString& msg) {
  if (out) *out = msg;
}

}  // namespace

QImage AiReader::decodeComposite(const QString& filePath, QString* errorOut) {
  // PDF 互換でない (旧 EPS ベース等) は先頭シグネチャで弾く。QPdfDocument に
  // 渡す前に判定しておくと、失敗理由を区別できる。
  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly)) {
    setError(errorOut, QStringLiteral("cannot open file"));
    return {};
  }
  const QByteArray head = f.read(1024);
  f.close();
  if (!head.contains("%PDF-")) {
    setError(errorOut, QStringLiteral("not a PDF-compatible .ai (legacy PostScript)"));
    return {};
  }

  // ワーカースレッドから呼ばれる。QPdfDocument はこのスレッドの上で完結させる。
  QPdfDocument doc;
  if (doc.load(filePath) != QPdfDocument::Error::None || doc.pageCount() <= 0) {
    setError(errorOut, QStringLiteral("failed to load as PDF"));
    return {};
  }

  const QSizeF pt = doc.pagePointSize(0);
  if (pt.isEmpty()) {
    setError(errorOut, QStringLiteral("empty artboard"));
    return {};
  }

  qreal scale = kScale;
  const qreal longSide = qMax(pt.width(), pt.height());
  if (longSide * scale > kMaxLongSidePx) scale = kMaxLongSidePx / longSide;
  const QSize size(qMax(1, qRound(pt.width() * scale)),
                   qMax(1, qRound(pt.height() * scale)));

  QPdfDocumentRenderOptions opts;
  QImage img = doc.render(0, size, opts);
  if (img.isNull()) {
    setError(errorOut, QStringLiteral("render failed"));
    return {};
  }
  return img;
}

}  // namespace Farman
