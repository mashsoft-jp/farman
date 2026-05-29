#pragma once

#include "WorkerBase.h"
#include <QStringList>

// libarchive の不透明構造体をグローバル名前空間で前方宣言しておく。
// namespace Farman 内に置くと Farman::archive として解釈されてしまう。
struct archive;

namespace Farman {

// 選択されたファイル／ディレクトリをアーカイブにまとめるワーカー。
// libarchive の write API を使用し、ディレクトリは再帰的に収める。
class ArchiveCreateWorker : public WorkerBase {
  Q_OBJECT

public:
  enum class Format {
    Zip,        // .zip
    Tar,        // .tar
    TarGz,      // .tar.gz
    TarBz2,     // .tar.bz2
    TarXz,      // .tar.xz
  };

  // zip の暗号化方式。zip 形式 + パスフレーズ設定時のみ有効。
  enum class Encryption {
    None,       // 暗号化しない
    Aes256,     // WinZip AES-256 (推奨)
    ZipCrypt,   // 旧式 ZipCrypto (脆弱だが互換性高)
  };

  ArchiveCreateWorker(const QString&     outputPath,
                      Format             format,
                      const QStringList& inputPaths,
                      QObject*           parent = nullptr);

  // 作成オプション (start() 前に任意で設定する)。
  // - パスフレーズ + 暗号化方式は **zip 形式のみ** 有効。空 / None で無効。
  //   要求した暗号化が libarchive ビルドで使えない場合は run() がエラー終了する
  //   (平文を黙って作らない)。
  // - 圧縮レベルは 0〜9。-1 (既定) で libarchive のデフォルトに任せる。
  //   無圧縮の Tar には影響しない。
  void setPassphrase(const QString& pass) { m_passphrase = pass; }
  void setEncryption(Encryption enc)      { m_encryption = enc; }
  void setCompressionLevel(int level)     { m_compressionLevel = level; }

protected:
  void run() override;

private:
  // 1 ファイル分をアーカイブに追加する。成功なら true。
  bool addEntry(::archive*     a,
                const QString& absPath,
                const QString& entryName);
  // ディレクトリを再帰走査して addEntry する
  bool addDirectoryRecursive(::archive*     a,
                             const QString& absPath,
                             const QString& entryName);

  QString     m_outputPath;
  Format      m_format;
  QStringList m_inputPaths;

  // 作成オプション (既定 = 暗号化なし / 圧縮レベルは libarchive 既定)。
  QString     m_passphrase;
  Encryption  m_encryption       = Encryption::None;
  int         m_compressionLevel = -1;
};

} // namespace Farman
