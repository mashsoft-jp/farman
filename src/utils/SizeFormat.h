#pragma once

#include "settings/Settings.h"
#include <QLocale>
#include <QString>

namespace Farman {

// 一覧以外 (選択合計 / ディスク空き容量 / 転送の進捗 / プロパティ / プレビュー /
// ビュアー / 検索結果) で使うサイズ表記。設定の generalSizeFormat に従う:
//   SI    : KB / MB / GB (1000 進)
//   IEC   : KiB / MiB / GiB (1024 進)
//   Bytes : 桁区切り付きのバイト数 ("12,345,678 B")
// 単位は英語固定 (システムロケールに依存させない)。
inline QString formatDataSize(qint64 bytes, int precision = 1) {
  const QLocale en(QLocale::English);
  switch (Settings::instance().generalSizeFormat()) {
    case FileSizeFormat::Bytes:
      return QStringLiteral("%1 B").arg(en.toString(bytes));
    case FileSizeFormat::IEC:
      return en.formattedDataSize(bytes, precision, QLocale::DataSizeIecFormat);
    case FileSizeFormat::SI:
    case FileSizeFormat::Auto:
    default:
      return en.formattedDataSize(bytes, precision, QLocale::DataSizeSIFormat);
  }
}

} // namespace Farman
