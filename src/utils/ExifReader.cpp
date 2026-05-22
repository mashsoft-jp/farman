#include "ExifReader.h"

#include <QFile>
#include <QtEndian>

#include <cstring>

namespace Farman {

namespace {

// ───── 低レベルパース補助 ─────────────────────────────

// バイトオーダ判定済みの状態で TIFF ストリームを走査するヘルパ。
// `base` は TIFF ヘッダ (= "II" or "MM" の先頭) のポインタで、IFD オフセットは
// すべて base からの相対値。`size` は TIFF データ全体のサイズ。
struct TiffReader {
  const uchar* base;
  qsizetype     size;
  bool          bigEndian;

  bool inRange(qsizetype offset, qsizetype length) const {
    if (offset < 0 || length < 0) return false;
    if (offset > size) return false;
    return (offset + length) <= size;
  }
  quint16 u16(qsizetype off) const {
    if (!inRange(off, 2)) return 0;
    return bigEndian ? qFromBigEndian<quint16>(base + off)
                     : qFromLittleEndian<quint16>(base + off);
  }
  quint32 u32(qsizetype off) const {
    if (!inRange(off, 4)) return 0;
    return bigEndian ? qFromBigEndian<quint32>(base + off)
                     : qFromLittleEndian<quint32>(base + off);
  }
  qint32 s32(qsizetype off) const { return static_cast<qint32>(u32(off)); }
};

// Exif タグ 1 件分の中間表現。
struct TagEntry {
  quint16 tag;
  quint16 type;     // 1=BYTE,2=ASCII,3=SHORT,4=LONG,5=RATIONAL,7=UNDEFINED,9=SLONG,10=SRATIONAL
  quint32 count;
  quint32 valueOrOffset;  // type/count の合計バイト数が 4 を超える場合はオフセット
};

// type のバイト幅。
int typeSize(quint16 type) {
  switch (type) {
    case 1: case 2: case 7: return 1;  // BYTE / ASCII / UNDEFINED
    case 3:                 return 2;  // SHORT
    case 4: case 9:         return 4;  // LONG / SLONG
    case 5: case 10:        return 8;  // RATIONAL / SRATIONAL
  }
  return 0;
}

// タグ値の格納位置を解決する: 値領域が <=4 バイトなら valueOrOffset の中、
// それ以外は valueOrOffset がオフセット。
qsizetype valueOffsetFor(const TagEntry& e, qsizetype entryAbsOffset) {
  const qsizetype total = static_cast<qsizetype>(typeSize(e.type))
                        * static_cast<qsizetype>(e.count);
  if (total <= 4) {
    // インライン: 値は valueOrOffset を入れている 4 バイトの中
    return entryAbsOffset + 8;  // IFD entry: tag(2) + type(2) + count(4) + value(4)
  }
  return static_cast<qsizetype>(e.valueOrOffset);
}

// タグから ASCII 文字列を読み出す (Exif の ASCII 型は NUL 終端あり)。
QString readAscii(const TiffReader& r, const TagEntry& e, qsizetype entryAbsOffset) {
  if (e.type != 2) return QString();
  const qsizetype off = valueOffsetFor(e, entryAbsOffset);
  if (!r.inRange(off, e.count)) return QString();
  const char* p = reinterpret_cast<const char*>(r.base + off);
  // 末尾 NUL を除く長さを安全に決定
  qsizetype len = 0;
  while (len < static_cast<qsizetype>(e.count) && p[len] != '\0') ++len;
  return QString::fromUtf8(p, static_cast<int>(len)).trimmed();
}

// タグから SHORT 1 件を読み出す。
quint16 readShort(const TiffReader& r, const TagEntry& e, qsizetype entryAbsOffset, int idx = 0) {
  if (e.type != 3 || idx >= static_cast<int>(e.count)) return 0;
  const qsizetype off = valueOffsetFor(e, entryAbsOffset);
  return r.u16(off + idx * 2);
}

// RATIONAL (分子 / 分母、各 LONG)。
struct Rational { quint32 num; quint32 den; };
Rational readRational(const TiffReader& r, const TagEntry& e, qsizetype entryAbsOffset, int idx = 0) {
  if (e.type != 5 || idx >= static_cast<int>(e.count)) return { 0, 0 };
  const qsizetype off = valueOffsetFor(e, entryAbsOffset);
  return { r.u32(off + idx * 8), r.u32(off + idx * 8 + 4) };
}

// SRATIONAL (符号付き)。
struct SRational { qint32 num; qint32 den; };
SRational readSRational(const TiffReader& r, const TagEntry& e, qsizetype entryAbsOffset, int idx = 0) {
  if (e.type != 10 || idx >= static_cast<int>(e.count)) return { 0, 0 };
  const qsizetype off = valueOffsetFor(e, entryAbsOffset);
  return { r.s32(off + idx * 8), r.s32(off + idx * 8 + 4) };
}

// ───── タグ → 人間可読変換 ─────────────────────────

QString formatRational(const Rational& q) {
  if (q.den == 0) return QStringLiteral("?");
  const double v = static_cast<double>(q.num) / static_cast<double>(q.den);
  return QString::number(v, 'g', 6);
}

QString formatExposureTime(const Rational& q) {
  if (q.den == 0 || q.num == 0) return QStringLiteral("?");
  if (q.num >= q.den) {
    // 1 秒以上
    const double v = static_cast<double>(q.num) / static_cast<double>(q.den);
    return QStringLiteral("%1 s").arg(QString::number(v, 'g', 4));
  }
  // 1/x 秒形式
  const double inv = static_cast<double>(q.den) / static_cast<double>(q.num);
  return QStringLiteral("1/%1 s").arg(QString::number(qRound(inv)));
}

QString formatFNumber(const Rational& q) {
  if (q.den == 0) return QStringLiteral("?");
  const double v = static_cast<double>(q.num) / static_cast<double>(q.den);
  return QStringLiteral("f/%1").arg(QString::number(v, 'g', 3));
}

QString formatFocalLength(const Rational& q) {
  if (q.den == 0) return QStringLiteral("?");
  const double v = static_cast<double>(q.num) / static_cast<double>(q.den);
  return QStringLiteral("%1 mm").arg(QString::number(v, 'g', 4));
}

QString orientationText(quint16 v) {
  switch (v) {
    case 1: return ExifReader::tr("Horizontal (normal)");
    case 2: return ExifReader::tr("Mirror horizontal");
    case 3: return ExifReader::tr("Rotate 180");
    case 4: return ExifReader::tr("Mirror vertical");
    case 5: return ExifReader::tr("Mirror horizontal and rotate 270 CW");
    case 6: return ExifReader::tr("Rotate 90 CW");
    case 7: return ExifReader::tr("Mirror horizontal and rotate 90 CW");
    case 8: return ExifReader::tr("Rotate 270 CW");
  }
  return QString::number(v);
}

QString flashText(quint16 v) {
  // 詳細フラグは多数あるが要点だけ。Bit 0 = Flash fired。
  const bool fired = (v & 0x01) != 0;
  return fired ? ExifReader::tr("Fired (raw 0x%1)").arg(v, 0, 16)
               : ExifReader::tr("Did not fire (raw 0x%1)").arg(v, 0, 16);
}

QString whiteBalanceText(quint16 v) {
  switch (v) {
    case 0: return ExifReader::tr("Auto");
    case 1: return ExifReader::tr("Manual");
  }
  return QString::number(v);
}

QString colorSpaceText(quint16 v) {
  switch (v) {
    case 1:       return ExifReader::tr("sRGB");
    case 0xFFFF:  return ExifReader::tr("Uncalibrated");
    case 2:       return ExifReader::tr("Adobe RGB");  // 一部メーカ非標準拡張
  }
  return QString::number(v);
}

// GPS DMS (度分秒) RATIONAL × 3 を 10進度数に変換し、Ref ('N'/'S'/'E'/'W') を反映。
QString formatGpsCoord(const TiffReader& r, const TagEntry& e, qsizetype entryAbsOffset,
                       const QString& refStr) {
  if (e.type != 5 || e.count < 3) return QString();
  const Rational d = readRational(r, e, entryAbsOffset, 0);
  const Rational m = readRational(r, e, entryAbsOffset, 1);
  const Rational s = readRational(r, e, entryAbsOffset, 2);
  if (d.den == 0 || m.den == 0 || s.den == 0) return QString();
  double v = static_cast<double>(d.num) / d.den
           + static_cast<double>(m.num) / m.den / 60.0
           + static_cast<double>(s.num) / s.den / 3600.0;
  if (refStr == QLatin1String("S") || refStr == QLatin1String("W")) v = -v;
  return QStringLiteral("%1° (%2)").arg(QString::number(v, 'f', 6), refStr);
}

// ───── IFD パース本体 ────────────────────────────

// `ifdOff` から始まる IFD を走査し、コールバックでタグを逐次返す。
// 戻り値は次の IFD オフセット (0 なら無し)。
template <class F>
quint32 walkIfd(const TiffReader& r, qsizetype ifdOff, F&& fn) {
  if (!r.inRange(ifdOff, 2)) return 0;
  const quint16 nEntries = r.u16(ifdOff);
  if (nEntries == 0 || nEntries > 1000) return 0;  // 異常防御
  qsizetype entryOff = ifdOff + 2;
  for (int i = 0; i < nEntries; ++i, entryOff += 12) {
    if (!r.inRange(entryOff, 12)) break;
    TagEntry e;
    e.tag           = r.u16(entryOff);
    e.type          = r.u16(entryOff + 2);
    e.count         = r.u32(entryOff + 4);
    e.valueOrOffset = r.u32(entryOff + 8);
    fn(e, entryOff);
  }
  // 次の IFD オフセット
  if (!r.inRange(entryOff, 4)) return 0;
  return r.u32(entryOff);
}

void parseGpsIfd(const TiffReader& r, qsizetype gpsIfdOff, ExifReader::Pairs& out) {
  QString latRef, lonRef;
  const TagEntry* latEnt = nullptr;
  const TagEntry* lonEnt = nullptr;
  qsizetype latEntOff = 0, lonEntOff = 0;

  // タグを 2 パス処理: 1 パス目で Ref を確定、2 パス目で値を整形
  // (ここでは Ref を先に拾うために、TagEntry を保存してから後でまとめる)
  QList<QPair<TagEntry, qsizetype>> entries;
  walkIfd(r, gpsIfdOff, [&](const TagEntry& e, qsizetype off) {
    entries.append({e, off});
  });

  // GPSAltitudeRef は BYTE: 0 = sea level (= 海抜上)、1 = below sea level。
  // タグが無い場合は 0 (= 海抜上) として扱う。
  quint16 altRef = 0;
  bool    altRefSet = false;
  for (const auto& pair : entries) {
    const TagEntry& e = pair.first;
    const qsizetype off = pair.second;
    switch (e.tag) {
      case 0x0001: latRef = readAscii(r, e, off); break;  // GPSLatitudeRef
      case 0x0003: lonRef = readAscii(r, e, off); break;  // GPSLongitudeRef
      case 0x0002: latEnt = &pair.first; latEntOff = off; break;  // GPSLatitude
      case 0x0004: lonEnt = &pair.first; lonEntOff = off; break;  // GPSLongitude
      case 0x0005: {  // GPSAltitudeRef (BYTE 1 byte)
        // BYTE 型はインライン格納 (4 バイト中の先頭バイト)。安全のため
        // valueOffsetFor を経由してオフセット解決し、その先頭バイトを読む。
        if (e.type == 1 && e.count >= 1) {
          const qsizetype voff = valueOffsetFor(e, off);
          if (r.inRange(voff, 1)) {
            altRef = r.base[voff];
            altRefSet = true;
          }
        }
        break;
      }
    }
  }

  // 緯度
  if (latEnt && !latRef.isEmpty()) {
    const QString s = formatGpsCoord(r, *latEnt, latEntOff, latRef);
    if (!s.isEmpty()) out.append({ ExifReader::tr("GPS Latitude"), s });
  }
  // 経度
  if (lonEnt && !lonRef.isEmpty()) {
    const QString s = formatGpsCoord(r, *lonEnt, lonEntOff, lonRef);
    if (!s.isEmpty()) out.append({ ExifReader::tr("GPS Longitude"), s });
  }

  // 高度 / タイムスタンプ / 日付
  for (const auto& pair : entries) {
    const TagEntry& e = pair.first;
    const qsizetype off = pair.second;
    switch (e.tag) {
      case 0x0006: {  // GPSAltitude (RATIONAL、絶対値)
        const Rational q = readRational(r, e, off);
        if (q.den != 0) {
          const double v = static_cast<double>(q.num) / static_cast<double>(q.den);
          const bool belowSeaLevel = altRefSet && altRef == 1;
          const QString s = belowSeaLevel
            ? QStringLiteral("-%1 m").arg(QString::number(v, 'g', 6))
            : QStringLiteral("%1 m").arg(QString::number(v, 'g', 6));
          out.append({ ExifReader::tr("GPS Altitude"), s });
        }
        break;
      }
      case 0x001D: {  // GPSDateStamp
        const QString s = readAscii(r, e, off);
        if (!s.isEmpty()) out.append({ ExifReader::tr("GPS Date"), s });
        break;
      }
      case 0x0007: {  // GPSTimeStamp (RATIONAL × 3: H/M/S)
        if (e.type == 5 && e.count == 3) {
          const Rational h = readRational(r, e, off, 0);
          const Rational m = readRational(r, e, off, 1);
          const Rational s = readRational(r, e, off, 2);
          if (h.den != 0 && m.den != 0 && s.den != 0) {
            const int hh = static_cast<int>(h.num / h.den);
            const int mm = static_cast<int>(m.num / m.den);
            const double ss = static_cast<double>(s.num) / s.den;
            out.append({ ExifReader::tr("GPS Time"),
                         QStringLiteral("%1:%2:%3 UTC")
                           .arg(hh, 2, 10, QLatin1Char('0'))
                           .arg(mm, 2, 10, QLatin1Char('0'))
                           .arg(QString::number(ss, 'f', 0)) });
          }
        }
        break;
      }
    }
  }
}

void parseExifIfd(const TiffReader& r, qsizetype exifIfdOff, ExifReader::Pairs& out) {
  walkIfd(r, exifIfdOff, [&](const TagEntry& e, qsizetype off) {
    switch (e.tag) {
      case 0x9003: {  // DateTimeOriginal
        const QString s = readAscii(r, e, off);
        if (!s.isEmpty()) out.append({ ExifReader::tr("Date Taken"), s });
        break;
      }
      case 0x9004: {  // DateTimeDigitized
        const QString s = readAscii(r, e, off);
        if (!s.isEmpty()) out.append({ ExifReader::tr("Date Digitized"), s });
        break;
      }
      case 0x829A: {  // ExposureTime
        const Rational q = readRational(r, e, off);
        if (q.den != 0) out.append({ ExifReader::tr("Exposure"), formatExposureTime(q) });
        break;
      }
      case 0x829D: {  // FNumber
        const Rational q = readRational(r, e, off);
        if (q.den != 0) out.append({ ExifReader::tr("Aperture"), formatFNumber(q) });
        break;
      }
      case 0x8827: {  // ISO
        const quint16 v = readShort(r, e, off);
        if (v != 0) out.append({ ExifReader::tr("ISO"), QString::number(v) });
        break;
      }
      case 0x920A: {  // FocalLength
        const Rational q = readRational(r, e, off);
        if (q.den != 0) out.append({ ExifReader::tr("Focal length"), formatFocalLength(q) });
        break;
      }
      case 0x9209: {  // Flash
        const quint16 v = readShort(r, e, off);
        out.append({ ExifReader::tr("Flash"), flashText(v) });
        break;
      }
      case 0x9208: {  // LightSource (白色点。WhiteBalance 0xA403 と別)
        // 通常ユーザは White Balance を見たいので LightSource は省略
        break;
      }
      case 0xA403: {  // WhiteBalance
        const quint16 v = readShort(r, e, off);
        out.append({ ExifReader::tr("White balance"), whiteBalanceText(v) });
        break;
      }
      case 0xA001: {  // ColorSpace
        const quint16 v = readShort(r, e, off);
        out.append({ ExifReader::tr("Color space"), colorSpaceText(v) });
        break;
      }
      case 0xA002: {  // ExifImageWidth
        const quint32 v = (e.type == 3) ? readShort(r, e, off) : r.u32(valueOffsetFor(e, off));
        if (v != 0) out.append({ ExifReader::tr("Exif image width"), QString::number(v) });
        break;
      }
      case 0xA003: {  // ExifImageHeight
        const quint32 v = (e.type == 3) ? readShort(r, e, off) : r.u32(valueOffsetFor(e, off));
        if (v != 0) out.append({ ExifReader::tr("Exif image height"), QString::number(v) });
        break;
      }
      case 0xA433: {  // LensMake
        const QString s = readAscii(r, e, off);
        if (!s.isEmpty()) out.append({ ExifReader::tr("Lens make"), s });
        break;
      }
      case 0xA434: {  // LensModel
        const QString s = readAscii(r, e, off);
        if (!s.isEmpty()) out.append({ ExifReader::tr("Lens model"), s });
        break;
      }
    }
  });
}

void parseIfd0(const TiffReader& r, qsizetype ifd0Off, ExifReader::Pairs& out,
               quint32& exifIfdOffset, quint32& gpsIfdOffset) {
  walkIfd(r, ifd0Off, [&](const TagEntry& e, qsizetype off) {
    switch (e.tag) {
      case 0x010F: {  // Make
        const QString s = readAscii(r, e, off);
        if (!s.isEmpty()) out.append({ ExifReader::tr("Camera make"), s });
        break;
      }
      case 0x0110: {  // Model
        const QString s = readAscii(r, e, off);
        if (!s.isEmpty()) out.append({ ExifReader::tr("Camera model"), s });
        break;
      }
      case 0x0112: {  // Orientation
        const quint16 v = readShort(r, e, off);
        out.append({ ExifReader::tr("Orientation"), orientationText(v) });
        break;
      }
      case 0x0131: {  // Software
        const QString s = readAscii(r, e, off);
        if (!s.isEmpty()) out.append({ ExifReader::tr("Software"), s });
        break;
      }
      case 0x0132: {  // DateTime
        const QString s = readAscii(r, e, off);
        if (!s.isEmpty()) out.append({ ExifReader::tr("Date modified"), s });
        break;
      }
      case 0x013B: {  // Artist
        const QString s = readAscii(r, e, off);
        if (!s.isEmpty()) out.append({ ExifReader::tr("Artist"), s });
        break;
      }
      case 0x8298: {  // Copyright
        const QString s = readAscii(r, e, off);
        if (!s.isEmpty()) out.append({ ExifReader::tr("Copyright"), s });
        break;
      }
      case 0x011A: {  // XResolution (RATIONAL)
        const Rational q = readRational(r, e, off);
        if (q.den != 0) out.append({ ExifReader::tr("X resolution"),
                                     formatRational(q) });
        break;
      }
      case 0x011B: {  // YResolution (RATIONAL)
        const Rational q = readRational(r, e, off);
        if (q.den != 0) out.append({ ExifReader::tr("Y resolution"),
                                     formatRational(q) });
        break;
      }
      case 0x8769: {  // ExifIFDPointer
        exifIfdOffset = e.valueOrOffset;
        break;
      }
      case 0x8825: {  // GPSIFDPointer
        gpsIfdOffset = e.valueOrOffset;
        break;
      }
    }
  });
}

// TIFF ヘッダ + IFD ストリームを受け取って Exif タグを抽出する共通関数。
// JPEG の APP1 / PNG の eXIf / WebP の EXIF / TIFF ファイル本体 から呼ばれる。
// `base`, `size` は "II"/"MM" の先頭からのバイト列。
void parseTiffStream(const uchar* base, qsizetype size, ExifReader::Pairs& out) {
  if (size < 8) return;
  TiffReader r;
  r.base = base;
  r.size = size;
  const uchar b0 = base[0];
  const uchar b1 = base[1];
  if (b0 == 'I' && b1 == 'I') {
    r.bigEndian = false;
  } else if (b0 == 'M' && b1 == 'M') {
    r.bigEndian = true;
  } else {
    return;
  }
  const quint16 magic = r.u16(2);
  if (magic != 0x002A) return;
  const quint32 ifd0Off = r.u32(4);

  quint32 exifIfdOff = 0;
  quint32 gpsIfdOff  = 0;
  parseIfd0(r, ifd0Off, out, exifIfdOff, gpsIfdOff);
  if (exifIfdOff != 0) parseExifIfd(r, exifIfdOff, out);
  if (gpsIfdOff  != 0) parseGpsIfd (r, gpsIfdOff,  out);
}

} // namespace

ExifReader::Pairs ExifReader::readFromJpeg(const QString& filePath) {
  Pairs out;

  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly)) return out;
  const QByteArray buf = f.readAll();
  f.close();
  if (buf.size() < 4) return out;
  // JPEG マジック SOI: FF D8
  if (static_cast<uchar>(buf[0]) != 0xFF || static_cast<uchar>(buf[1]) != 0xD8) {
    return out;
  }

  // JPEG セグメントを走査して APP1 (FF E1) + "Exif\0\0" を探す。
  qsizetype p = 2;
  while (p + 4 <= buf.size()) {
    if (static_cast<uchar>(buf[p]) != 0xFF) break;
    const uchar marker = static_cast<uchar>(buf[p + 1]);
    if (marker == 0xD9) break;          // EOI
    if (marker == 0xDA) break;          // SOS (圧縮データ開始 → これ以降は無視)
    if (marker == 0xD8) { p += 2; continue; }  // SOI 重複は飛ばす

    // セグメント長は 2 バイト big-endian
    const qsizetype segLen = (static_cast<uchar>(buf[p + 2]) << 8)
                           | static_cast<uchar>(buf[p + 3]);
    if (segLen < 2 || p + 2 + segLen > buf.size()) break;

    if (marker == 0xE1) {  // APP1
      // "Exif\0\0" マジック
      const qsizetype payloadOff = p + 4;
      const qsizetype payloadLen = segLen - 2;
      if (payloadLen >= 6) {
        if (std::memcmp(buf.constData() + payloadOff, "Exif\0\0", 6) == 0) {
          const qsizetype tiffOff = payloadOff + 6;
          const qsizetype tiffLen = payloadLen - 6;
          parseTiffStream(reinterpret_cast<const uchar*>(buf.constData() + tiffOff),
                          tiffLen, out);
          return out;  // Exif APP1 は通常 1 つだけ
        }
      }
    }
    p += 2 + segLen;
  }
  return out;
}

ExifReader::Pairs ExifReader::readFromPng(const QString& filePath) {
  Pairs out;
  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly)) return out;
  const QByteArray buf = f.readAll();
  f.close();

  // PNG マジック: 89 50 4E 47 0D 0A 1A 0A
  static const uchar kPngSig[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
  if (buf.size() < 8
      || std::memcmp(buf.constData(), kPngSig, 8) != 0) {
    return out;
  }

  // 8 バイト目以降をチャンクで走査。
  //   length (4 bytes big-endian) + type (4 bytes ASCII) + data (length bytes)
  //   + CRC (4 bytes)
  // "eXIf" チャンクが見つかったら、その data 部分がそのまま TIFF/Exif ストリーム。
  qsizetype p = 8;
  while (p + 8 <= buf.size()) {
    const auto* base = reinterpret_cast<const uchar*>(buf.constData() + p);
    const quint32 length = (quint32(base[0]) << 24)
                         | (quint32(base[1]) << 16)
                         | (quint32(base[2]) << 8)
                         |  quint32(base[3]);
    const QByteArray type = QByteArray(reinterpret_cast<const char*>(base + 4), 4);

    if (p + 8 + qsizetype(length) + 4 > buf.size()) break;  // 不正長
    if (type == QByteArray("eXIf", 4)) {
      const auto* data = reinterpret_cast<const uchar*>(buf.constData() + p + 8);
      parseTiffStream(data, qsizetype(length), out);
      return out;
    }
    if (type == QByteArray("IEND", 4)) break;  // 終端
    p += 8 + qsizetype(length) + 4;
  }
  return out;
}

ExifReader::Pairs ExifReader::readFromWebp(const QString& filePath) {
  Pairs out;
  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly)) return out;
  const QByteArray buf = f.readAll();
  f.close();

  // WebP コンテナ (RIFF):
  //   "RIFF" (4) + fileSize (4, little-endian) + "WEBP" (4)
  //   その後ろにチャンク群: type(4) + size(4 LE) + data(size) + パディング 1B (size 奇数のとき)
  // 拡張形式 (VP8X) なら "EXIF" チャンクが入る可能性がある。
  if (buf.size() < 12) return out;
  if (std::memcmp(buf.constData(), "RIFF", 4) != 0) return out;
  if (std::memcmp(buf.constData() + 8, "WEBP", 4) != 0) return out;

  qsizetype p = 12;
  while (p + 8 <= buf.size()) {
    const auto* base = reinterpret_cast<const uchar*>(buf.constData() + p);
    const QByteArray type = QByteArray(reinterpret_cast<const char*>(base), 4);
    const quint32 size =  quint32(base[4])
                       | (quint32(base[5]) <<  8)
                       | (quint32(base[6]) << 16)
                       | (quint32(base[7]) << 24);
    if (p + 8 + qsizetype(size) > buf.size()) break;

    if (type == QByteArray("EXIF", 4)) {
      // EXIF チャンクは中身がそのまま TIFF/Exif ストリーム。
      // (一部の WebP ライターは先頭に "Exif\0\0" マジックを付けてしまうので、
      //  それが見つかった場合はスキップする防御を入れる)
      const uchar* data = base + 8;
      qsizetype dataLen = qsizetype(size);
      if (dataLen >= 6 && std::memcmp(data, "Exif\0\0", 6) == 0) {
        data    += 6;
        dataLen -= 6;
      }
      parseTiffStream(data, dataLen, out);
      return out;
    }
    // 次のチャンクへ。size が奇数ならパディング 1 バイトを跨ぐ。
    qsizetype step = 8 + qsizetype(size);
    if (size & 1) ++step;
    p += step;
  }
  return out;
}

ExifReader::Pairs ExifReader::readFromTiff(const QString& filePath) {
  Pairs out;
  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly)) return out;
  const QByteArray buf = f.readAll();
  f.close();
  if (buf.size() < 8) return out;
  // TIFF はファイル先頭が直接 TIFF ヘッダ。コンテナ層は無いので
  // バイト列全体を parseTiffStream に渡すだけ。
  parseTiffStream(reinterpret_cast<const uchar*>(buf.constData()),
                  qsizetype(buf.size()), out);
  return out;
}

ExifReader::Pairs ExifReader::readForFormat(const QString& filePath,
                                            const QString& formatName) {
  const QString f = formatName.toLower();
  if (f == QLatin1String("jpeg") || f == QLatin1String("jpg")) {
    return readFromJpeg(filePath);
  }
  if (f == QLatin1String("png")) {
    return readFromPng(filePath);
  }
  if (f == QLatin1String("webp")) {
    return readFromWebp(filePath);
  }
  if (f == QLatin1String("tiff") || f == QLatin1String("tif")) {
    return readFromTiff(filePath);
  }
  return {};
}

} // namespace Farman
