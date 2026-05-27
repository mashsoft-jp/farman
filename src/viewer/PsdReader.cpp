#include "PsdReader.h"

#include <QByteArray>
#include <QFile>
#include <QVector>
#include <QtEndian>

#include <cstring>

namespace Farman {

namespace {

// PSD ファイルは整数値が **big-endian** で格納される。Qt の `qFromBigEndian`
// を使って読み出すラッパ。読み込めなかった場合は 0 を返す (呼び出し側で
// pos / 期待長と突き合わせて検出する)。
template <typename T>
T readBE(QFile& f) {
  T raw{};
  if (f.read(reinterpret_cast<char*>(&raw), sizeof(T)) != sizeof(T)) return T{};
  return qFromBigEndian(raw);
}

// PackBits / RLE 解凍。Photoshop は PSD の RLE 圧縮にこのアルゴリズムを使う。
//   - 1 バイト n を読んで:
//       n >= 0     : 続く (n+1) バイトをそのままコピー (literal)
//       n in [-127,-1]: 続く 1 バイトを (1-n) 回繰り返してコピー (run)
//       n == -128  : no-op (パディング)
// 入力 srcLen バイトから出力 dstLen バイトちょうど生成できれば true。
bool unpackBits(const char* src, qsizetype srcLen,
                 char* dst, qsizetype dstLen) {
  qsizetype si = 0, di = 0;
  while (si < srcLen && di < dstLen) {
    const int8_t n = static_cast<int8_t>(src[si++]);
    if (n >= 0) {
      const int count = n + 1;
      if (si + count > srcLen) return false;
      if (di + count > dstLen) return false;
      std::memcpy(dst + di, src + si, count);
      si += count;
      di += count;
    } else if (n != -128) {
      const int count = 1 - n;   // 1..128
      if (si >= srcLen) return false;
      if (di + count > dstLen) return false;
      std::memset(dst + di, src[si], count);
      ++si;
      di += count;
    }
    // n == -128 は no-op
  }
  return di == dstLen;
}

} // namespace

QImage PsdReader::decodeComposite(const QString& filePath, QString* errorOut) {
  auto fail = [&](const QString& msg) {
    if (errorOut) *errorOut = msg;
    return QImage();
  };

  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly)) {
    return fail(QStringLiteral("Cannot open PSD file"));
  }

  // ───── Header (26 バイト固定) ─────
  char sig[4];
  if (f.read(sig, 4) != 4 || std::memcmp(sig, "8BPS", 4) != 0) {
    return fail(QStringLiteral("Not a PSD file (signature mismatch)"));
  }
  const quint16 version = readBE<quint16>(f);
  if (version != 1) {
    return fail(QStringLiteral("Unsupported PSD version %1 (PSB not supported)")
                  .arg(version));
  }
  // 6 バイト reserved
  if (!f.seek(f.pos() + 6)) return fail(QStringLiteral("Header truncated"));

  const quint16 channels = readBE<quint16>(f);
  const quint32 height   = readBE<quint32>(f);
  const quint32 width    = readBE<quint32>(f);
  const quint16 depth    = readBE<quint16>(f);
  const quint16 colorMode = readBE<quint16>(f);

  if (width == 0 || height == 0 || channels == 0) {
    return fail(QStringLiteral("Invalid PSD dimensions / channel count"));
  }
  if (depth != 8) {
    return fail(QStringLiteral("Unsupported bit depth %1 (only 8 bpc supported)")
                  .arg(depth));
  }
  // 1 = Grayscale, 3 = RGB。それ以外 (CMYK / Lab / Indexed / Bitmap) は非対応。
  if (colorMode != 1 && colorMode != 3) {
    return fail(QStringLiteral("Unsupported color mode %1 "
                                "(only Grayscale / RGB supported)")
                  .arg(colorMode));
  }

  // ───── 各セクションをスキップ ─────
  // Color Mode Data (Indexed / Duotone でない限り長さ 0)
  const quint32 cmLen = readBE<quint32>(f);
  if (!f.seek(f.pos() + cmLen))
    return fail(QStringLiteral("Color mode block truncated"));

  // Image Resources (resolution / thumbnail / ICC 等のメタ)
  const quint32 irLen = readBE<quint32>(f);
  if (!f.seek(f.pos() + irLen))
    return fail(QStringLiteral("Image resources block truncated"));

  // Layer and Mask Information (個別レイヤー本体。本リーダでは中身は見ない)
  const quint32 liLen = readBE<quint32>(f);
  if (!f.seek(f.pos() + liLen))
    return fail(QStringLiteral("Layer info block truncated"));

  // ───── Image Data Block (= 合成済プレビュー) ─────
  // ファイル末尾までが合成済プレビュー領域。Photoshop は "Maximize
  // Compatibility" 設定が ON のとき、ここに全レイヤーをマージした
  // 完成形を planar (R 全体 → G 全体 → B 全体 → A 全体) で書く。
  const quint16 compression = readBE<quint16>(f);
  if (compression != 0 && compression != 1) {
    return fail(QStringLiteral("Unsupported compression %1 "
                                "(only Raw / RLE supported)")
                  .arg(compression));
  }

  // 1 チャネル分のバイト数 = width * height (8bpc)
  const qsizetype channelSize = qsizetype(width) * qsizetype(height);
  // 全チャネル分。合計サイズ overflow を一応チェック。
  if (channelSize <= 0 || channels > 56) {
    return fail(QStringLiteral("Invalid PSD channel size"));
  }
  const qsizetype totalSize = channelSize * qsizetype(channels);

  QByteArray planar(totalSize, Qt::Uninitialized);

  if (compression == 0) {
    // Raw: 各 channel の pixel 配列が連結されているだけ。
    if (f.read(planar.data(), totalSize) != totalSize) {
      return fail(QStringLiteral("Image data truncated (raw)"));
    }
  } else {
    // RLE: 先頭に「channel * height」個の 2 バイト scanline 長テーブル、
    // 続いて scanline ごとに PackBits 圧縮された data 本体。
    const qsizetype numScanlines = qsizetype(height) * qsizetype(channels);
    QVector<quint16> scanlineCounts(numScanlines);
    for (qsizetype i = 0; i < numScanlines; ++i) {
      scanlineCounts[i] = readBE<quint16>(f);
    }
    // 解凍先は planar 配列の channel * row 位置。
    for (qsizetype ch = 0; ch < channels; ++ch) {
      for (qsizetype row = 0; row < qsizetype(height); ++row) {
        const quint16 compLen = scanlineCounts[ch * qsizetype(height) + row];
        const QByteArray compData = f.read(compLen);
        if (compData.size() != compLen) {
          return fail(QStringLiteral("RLE scanline truncated"));
        }
        char* dst = planar.data() + ch * channelSize + row * qsizetype(width);
        if (!unpackBits(compData.constData(), compLen, dst, qsizetype(width))) {
          return fail(QStringLiteral(
            "RLE decompression failed at channel %1 row %2").arg(ch).arg(row));
        }
      }
    }
  }

  // ───── planar → QImage 変換 ─────
  // PSD は channel 順に R, G, B, A, (extras...) と並ぶ。標準 channel 以外
  // (5+, 選択範囲 / spot color 等) は無視する。
  if (colorMode == 1) {
    // Grayscale (channels >= 1)
    QImage img(int(width), int(height), QImage::Format_Grayscale8);
    for (quint32 y = 0; y < height; ++y) {
      const char* src = planar.constData() + qsizetype(y) * qsizetype(width);
      std::memcpy(img.scanLine(int(y)), src, width);
    }
    return img;
  }

  // RGB / RGBA
  const bool hasAlpha = channels >= 4;
  const QImage::Format fmt = hasAlpha ? QImage::Format_RGBA8888
                                       : QImage::Format_RGB888;
  QImage img(int(width), int(height), fmt);
  const uchar* basePtr = reinterpret_cast<const uchar*>(planar.constData());
  for (quint32 y = 0; y < height; ++y) {
    const uchar* r = basePtr + 0 * channelSize + qsizetype(y) * qsizetype(width);
    const uchar* g = basePtr + 1 * channelSize + qsizetype(y) * qsizetype(width);
    const uchar* b = basePtr + 2 * channelSize + qsizetype(y) * qsizetype(width);
    const uchar* a = hasAlpha
                      ? basePtr + 3 * channelSize + qsizetype(y) * qsizetype(width)
                      : nullptr;
    uchar* dst = img.scanLine(int(y));
    const int bpp = hasAlpha ? 4 : 3;
    for (quint32 x = 0; x < width; ++x) {
      dst[x * bpp + 0] = r[x];
      dst[x * bpp + 1] = g[x];
      dst[x * bpp + 2] = b[x];
      if (hasAlpha) dst[x * bpp + 3] = a[x];
    }
  }
  return img;
}

} // namespace Farman
