#pragma once

#include "PluginCatalog.h"

#include <QObject>
#include <QString>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace Farman {

// 公式プラグイン 1 件をダウンロードして SHA256 を照合する。照合に通ったファイルの
// パスを finished で返すので、呼び出し側が PluginInstaller (検証 → 退避) に渡す。
//
// 本体の自動アップデート (UpdateDownloader) と同じ手順だが、こちらは **.sha256 を必須**
// とし、取得できない / 一致しない場合は失敗にする (公式一覧からの導入では、入手元の
// 確認をユーザーに求めない代わりに照合を必ず行う)。ダウンロード元は、マニフェストの
// repo のリリース配布物 (https://github.com/<repo>/releases/download/) に限る。
class PluginDownloader : public QObject {
  Q_OBJECT

public:
  explicit PluginDownloader(QObject* parent = nullptr);
  ~PluginDownloader() override;

  // 複数同時実行はしない。進行中なら何もしない。
  void start(const PluginCatalogEntry& entry);
  void cancel();
  bool isRunning() const { return m_state != State::Idle; }

signals:
  // totalBytes が 0 なら不確定。
  void progress(qint64 receivedBytes, qint64 totalBytes);
  // ok=true なら filePath に照合済みファイル。ok=false なら errorReason に理由
  // (ユーザー向けの翻訳済み文字列)。cancel() のときは出さない。
  void finished(bool ok, const QString& filePath, const QString& errorReason);

private:
  enum class State { Idle, FetchingSha256, Downloading };

  void beginDownload();
  void onSha256Finished();
  void onAssetFinished();
  void fail(const QString& reason);
  void cleanup();

  PluginCatalogEntry     m_entry;
  QString                m_expectedSha256;
  QString                m_savePath;
  QString                m_partPath;
  QFile*                 m_partFile = nullptr;
  QNetworkAccessManager* m_nam = nullptr;
  QNetworkReply*         m_reply = nullptr;
  State                  m_state = State::Idle;
};

} // namespace Farman
