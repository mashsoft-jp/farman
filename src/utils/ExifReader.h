#pragma once

#include <QCoreApplication>
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
  Q_DECLARE_TR_FUNCTIONS(ExifReader)
public:
  using Pair  = QPair<QString, QString>;
  using Pairs = QList<Pair>;

  // JPEG ファイルから主要 Exif タグを抽出。
  // 非 JPEG / 失敗時は空リスト。
  static Pairs readFromJpeg(const QString& filePath);

  // PNG ファイルの eXIf チャンクから主要 Exif タグを抽出。
  // PNG 仕様 v2.0 (2017) で追加された eXIf チャンクの中身は JPEG と同じ
  // TIFF/Exif バイトストリーム。チャンクが無い場合は空リスト。
  static Pairs readFromPng(const QString& filePath);

  // WebP の EXIF チャンク (拡張形式 WebP のみ) から主要 Exif タグを抽出。
  // チャンクが無い場合 (= 最小形式 WebP / Exif 無し画像) は空リスト。
  static Pairs readFromWebp(const QString& filePath);

  // TIFF ファイル (`.tif` / `.tiff`) を直接パース。ファイル先頭がそのまま
  // TIFF ヘッダ (II/MM + 0x002A + IFD0 offset) なので、コンテナ層なし。
  static Pairs readFromTiff(const QString& filePath);

  // フォーマット名 (QImageReader::format() が返す大文字小文字混在の文字列、
  // 例 "jpeg" / "JPEG" / "png" / "webp" / "tiff") から自動振り分け。
  // 該当する関数を呼んでその結果を返す。未対応形式は空リスト。
  static Pairs readForFormat(const QString& filePath, const QString& formatName);
};

} // namespace Farman
