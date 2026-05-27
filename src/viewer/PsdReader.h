#pragma once

#include <QImage>
#include <QString>

namespace Farman {

// PSD (Adobe Photoshop Document) 形式の最小限のリーダ。
//
// 役割は「Photoshop が保存時に末尾に書き出す **合成済プレビュー画像**
// (= 全レイヤーをマージした完成形 RGBA) を `QImage` として取り出すこと」だけ。
// Qt 6 同梱の imageformats プラグインには PSD ハンドラが無いので、
// `QImageReader::read()` が失敗したケースのフォールバックとして使う。
//
// 対応:
//   - PSD v1 (`.psd`、画像サイズ <= 30000 px)
//   - 8 bpc Grayscale / RGB / RGBA
//   - Raw / RLE (PackBits) 圧縮
//
// 非対応 (= ファイルマネージャのビュアーとしては過剰):
//   - PSB (v2、大型形式)
//   - 16/32 bpc、CMYK / Lab / Indexed / Bitmap
//   - レイヤー個別表示 / 効果 / マスク
//   - ZIP / ZIPp 圧縮 (実物にあまり遭遇しない)
//   - "Maximize Compatibility" を OFF にして保存された PSD (= 合成済プレ
//     ビューが書き出されていない。ファイル自体としては有効だが、Photoshop
//     以外では中身を再現できない)
//
// 失敗時は `QImage()` (null) を返し、`errorOut` に英語のエラー文字列を入れる。
class PsdReader {
public:
  static QImage decodeComposite(const QString& filePath,
                                 QString* errorOut = nullptr);
};

} // namespace Farman
