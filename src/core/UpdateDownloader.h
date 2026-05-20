#pragma once

#include "UpdateChecker.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace Farman {

// 選択されたリリースから、現在の OS / arch に合うアセット 1 件をダウンロード
// + SHA256 検証して、プラットフォーム別のインストール経路に渡す責務。
//
// 流れ (使い手 = MainWindow):
//   auto* dl = new UpdateDownloader(this);
//   connect(dl, &UpdateDownloader::progress,  ...);  // %, bytes
//   connect(dl, &UpdateDownloader::finished, ...);   // ok / errorReason
//   dl->start(releaseInfo);
//
// 1. assets[] から OS/arch にマッチする 1 件を選ぶ (selectAsset)
//    (macos-arm64.dmg / windows-x64-setup.exe / linux-x86_64.AppImage)
// 2. 同名 + ".sha256" のアセットがあれば期待値を取得 (任意、無ければ検証スキップ)
// 3. AppCacheLocation/updates/ に保存しながら本体をダウンロード
// 4. SHA256 を計算して照合
// 5. install() を呼んでプラットフォーム別の置換シーケンスを起動 (自プロセスは
//    終了させる)
class UpdateDownloader : public QObject {
  Q_OBJECT

public:
  explicit UpdateDownloader(QObject* parent = nullptr);
  ~UpdateDownloader() override;

  // 与えられたリリースから自分の OS/arch に合うアセットをダウンロード + 検証 +
  // インストール起動する。複数同時実行はせず、進行中なら no-op。
  void start(const ReleaseInfo& release);
  bool isRunning() const { return m_state != State::Idle; }

  // 現在の OS / arch に対するアセット名のパターン (例: "macos-arm64.dmg")。
  // assets[] からこのパターンを末尾に含むものを選ぶ (バージョン部分は可変)。
  static QString assetPatternForThisPlatform();

  // 選択したアセットをローカル一時ファイルに保存しただけ (Phase C テスト用)。
  // インストール起動を呼ばず、ダウンロードと検証のみ実行する。
  void setInstallEnabled(bool enabled) { m_installEnabled = enabled; }

signals:
  // 進捗。totalBytes が 0 なら不確定。
  void progress(qint64 receivedBytes, qint64 totalBytes);
  // フェーズが変わったとき表示用に通知 ("Downloading...", "Verifying...",
  // "Installing...")。
  void phaseChanged(const QString& phaseLabel);
  // 完了。ok=false なら errorReason に理由が入る。
  void finished(bool ok, const QString& errorReason);

private slots:
  void onAssetReadyRead();
  void onAssetDownloadProgress(qint64 received, qint64 total);
  void onAssetFinished();
  void onSha256Finished();

private:
  enum class State { Idle, FetchingSha256, Downloading, Verifying, Installing };

  void fetchSha256();
  void beginDownload();
  bool verifySha256();
  // プラットフォーム別の install を起動。返値 ok / 失敗理由は finished で。
  void runInstall();

  ReleaseAsset m_asset;          // 選択された本体アセット
  ReleaseAsset m_sha256Asset;    // 対応する .sha256 (任意)
  QString      m_expectedSha256; // .sha256 ファイルから読みとった hex 文字列

  QString      m_savePath;       // 本体の保存先 (AppCacheLocation/updates/...)
  QString      m_downloadTmpPath;// ダウンロード中 (.part) ファイル
  qint64       m_receivedBytes = 0;
  qint64       m_totalBytes    = 0;

  QNetworkAccessManager* m_nam = nullptr;
  QNetworkReply*         m_reply = nullptr;
  State                  m_state = State::Idle;
  bool                   m_installEnabled = true;
};

} // namespace Farman
