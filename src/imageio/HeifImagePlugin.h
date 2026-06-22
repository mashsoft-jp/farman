#pragma once

// libheif を使う HEIF / HEIC 読み込み用の QImageIOHandler プラグイン。
// QImageReader のプラグインとして登録されるので、本体・画像ビュアー・
// サムネイル・Info ダイアログなど QImageReader を使う全経路で HEIC が
// 透過的に読めるようになる。デコードのみ (書き込みは非対応)。
//
// macOS は Qt 同梱の qmacheif (Apple ImageIO) が HEIC を読むため、
// このプラグインは libheif が見つかるプラットフォーム (Windows / Linux)
// でのみビルドし、本体へ静的リンクして Q_IMPORT_PLUGIN で取り込む。

#include <QImageIOHandler>
#include <QImageIOPlugin>

// Q_IMPORT_PLUGIN は名前空間付きクラス名を扱えないため、プラグインクラスは
// グローバル名前空間に置く (farman の他コードからは参照しない)。

class HeifHandler : public QImageIOHandler {
public:
  HeifHandler() = default;

  bool canRead() const override;
  bool read(QImage* image) override;

  // device の内容が HEIF/HEIC かどうかを先頭バイトから判定する (peek のみ)。
  static bool canReadFrom(QIODevice* device);
};

class HeifPlugin : public QImageIOPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QImageIOHandlerFactoryInterface"
                    FILE "heif.json")

public:
  Capabilities capabilities(QIODevice* device,
                            const QByteArray& format) const override;
  QImageIOHandler* create(QIODevice* device,
                          const QByteArray& format = QByteArray()) const override;
};
