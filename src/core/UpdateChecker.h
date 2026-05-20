#pragma once

#include <QList>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace Farman {

// GitHub Releases API から取得したアセット 1 件 (OS 別の DMG / setup.exe /
// AppImage 等)。Phase C のダウンロード時に OS/arch に応じて選び出す。
struct ReleaseAsset {
  QString name;          // ファイル名 (例: "farman-v1.0.0-macos-arm64.dmg")
  QString downloadUrl;   // ダウンロード URL
  qint64  sizeBytes = 0; // バイト数
};

// GitHub Releases API から取得したリリース情報。
struct ReleaseInfo {
  QString version;     // "0.9.0" 形式 (tag_name から "v" を剥がしたもの)
  QString tagName;     // "v0.9.0" 等の生 tag 名
  QString name;        // リリース名 (空のことあり)
  QString body;        // リリースノート本文 (Markdown)
  QString htmlUrl;     // ブラウザで開く URL (https://github.com/.../releases/tag/v0.9.0)
  bool    prerelease = false;
  bool    draft      = false;
  QList<ReleaseAsset> assets;
};

// 更新チェッカ。GitHub Releases API (latest endpoint) を叩いて、現在の
// FARMAN_VERSION と比較する。失敗 (オフライン / 4xx / タイムアウト) は静かに
// finished(false, {}) を返すだけ (エラーダイアログは呼び出し側で出さない / 出さない
// のが SPEC.md の方針)。
//
// 使い方:
//   auto* c = new UpdateChecker(this);
//   connect(c, &UpdateChecker::finished, this,
//           [](bool ok, const ReleaseInfo& info, bool isNewer) { ... });
//   c->checkLatest();
//
// 同じインスタンスを使い回す前提 (QNetworkAccessManager を 1 つ持ち続ける)。
class UpdateChecker : public QObject {
  Q_OBJECT

public:
  explicit UpdateChecker(QObject* parent = nullptr);
  ~UpdateChecker() override;

  // GitHub から latest release を取得し、現在版と比較する。結果は finished
  // シグナルで返る。呼び出し側で多重発火を防ぐため、進行中なら no-op。
  void checkLatest();
  bool isChecking() const { return m_inflight; }

  // 2 つのバージョン文字列 (例: "0.9.0", "1.0.0", "1.0.0-test") を比較する。
  // 戻り値: a < b → -1、a == b → 0、a > b → +1。-test / -beta 等の
  // prerelease サフィックスは「数値部分が同じなら本番版 < prerelease は本番版より
  // 小さい」semver 風に扱う (= "1.0.0-test" < "1.0.0")。
  static int compareVersions(const QString& a, const QString& b);

signals:
  // 成功 ok=true: info が埋まっていて、isNewer は「リモートが現在版より新しいか」。
  // 失敗 ok=false: info は空、isNewer は false。errorReason はログ用 (UI に出さない)。
  void finished(bool ok, const Farman::ReleaseInfo& info, bool isNewer,
                const QString& errorReason);

private slots:
  void onReplyFinished();

private:
  QNetworkAccessManager* m_nam = nullptr;
  QNetworkReply*         m_reply = nullptr;
  bool                   m_inflight = false;

  // GitHub Releases JSON をパースして ReleaseInfo に変換。失敗時は ok=false。
  static bool parseReleaseJson(const QByteArray& json, ReleaseInfo* out,
                                QString* errorReason);
  // 現在の FARMAN_VERSION 文字列を取得 (compile-time マクロ経由)。
  static QString currentVersion();
  // User-Agent ヘッダ用文字列: "farman/<version> <os>/<arch>"。
  static QString userAgent();
};

} // namespace Farman
