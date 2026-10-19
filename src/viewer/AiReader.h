#pragma once

#include <QImage>
#include <QString>

namespace Farman {

// Adobe Illustrator (.ai) の合成プレビューを取り出す最小限のリーダ。
//
// 現行の .ai は先頭が `%PDF` の PDF 互換ファイルなので、PDF エンジン (QtPdf) で
// 第 1 ページ (= 第 1 アートボード) を描画して `QImage` にする。
// Qt 6 同梱の imageformats プラグインには .ai ハンドラが無いので、
// `QImageReader::read()` が失敗したケースのフォールバックとして使う (PsdReader と同じ位置付け)。
//
// 対応:
//   - PDF 互換の .ai (先頭が `%PDF`)
//   - 第 1 アートボードのみ。背景は透明 (ビュアーの透過設定で市松 / 単色に見える)
//
// 非対応:
//   - 旧 EPS ベース (PostScript) の .ai
//   - 「PDF 互換ファイルを作成」を OFF にして保存した .ai (PDF 部分が無い)
//   - 第 2 アートボード以降 / レイヤー個別表示
//   - パスワード保護
//
// 失敗時は `QImage()` (null) を返し、`errorOut` に英語のエラー文字列を入れる。
class AiReader {
public:
  // 第 1 アートボードを 2 倍 (144 dpi 相当) で描画する。長辺は 4096 px で頭打ち。
  static QImage decodeComposite(const QString& filePath,
                                QString* errorOut = nullptr);
};

} // namespace Farman
