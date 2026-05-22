#pragma once

#include <QList>
#include <QPair>
#include <QString>

namespace Farman {

// JPEG ファイルから Exif メタデータの主要タグを取り出す軽量パーサ。
//
//   - 依存ライブラリ無し (libexif / exiv2 を導入しない)
//   - JPEG の APP1 セグメントから "Exif\0\0" マジックを探し、その後の
//     TIFF ヘッダ (バイトオーダ MM/II + マジック 0x002A + 最初の IFD オフセット)
//     を解析、IFD0 → ExifIFD (Tag 0x8769) → GPS IFD (Tag 0x8825) を辿って
//     主要タグを抽出する。
//   - 抽出対象タグは「人間が見て嬉しい範囲」に絞る (全タグ展開はしない):
//       IFD0: Make / Model / Orientation / DateTime / Software /
//             Artist / Copyright / XResolution / YResolution
//       Exif IFD: DateTimeOriginal / DateTimeDigitized / ExposureTime /
//                 FNumber / ISO / FocalLength / Flash / WhiteBalance /
//                 ColorSpace / ExifImageWidth / ExifImageHeight /
//                 LensMake / LensModel
//       GPS IFD: 緯度 / 経度 / 高度 (+/-) / タイムスタンプ
//   - 戻り値は (キー, 値) ペアの順序付きリスト (UI で順番通り表示するため)。
//   - 解析失敗 / Exif 無しのときは空リスト。例外は投げない。
class ExifReader {
public:
  using Pair  = QPair<QString, QString>;
  using Pairs = QList<Pair>;

  // JPEG ファイルから主要 Exif タグを抽出。
  // 非 JPEG / 失敗時は空リスト。
  static Pairs readFromJpeg(const QString& filePath);
};

} // namespace Farman
