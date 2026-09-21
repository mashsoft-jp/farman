#pragma once

#include "PluginInstaller.h"
#include "UpdateChecker.h"   // ReleaseAsset

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace Farman {

// 公式プラグイン 1 件。マニフェスト (docs/plugins/manifest.json) の内容に、
// GitHub Releases から取った最新リリースの情報を足したもの。
struct PluginCatalogEntry {
  // ── マニフェスト ──
  QString id;                 // pluginId() と同じ (例 "lzh_archive")
  QString repo;               // "owner/farman-plugin-<name>"
  PluginInstaller::Kind kind = PluginInstaller::Kind::Unknown;
  QString fileName;           // 配布ファイル名の基底 (例 "LzhArchivePlugin")
  QString minFarmanVersion;   // 空 = 制限なし
  QString nameJa, nameEn;
  QString descriptionJa, descriptionEn;

  // ── 最新リリース ──
  enum class ReleaseState {
    Unknown,              // まだ取得していない
    Ok,                   // このプラットフォーム用のアセットと .sha256 が揃っている
    NoAssetForPlatform,   // リリースはあるが、この OS / arch 用の配布物が無い
    Failed,               // 取得失敗 (オフライン / API 上限 など)。releaseError に理由
  };
  ReleaseState releaseState = ReleaseState::Unknown;
  QString      latestVersion;   // "1.0.0" (tag から "v" を剥がしたもの)
  QString      releaseUrl;      // リリースページ
  QString      releaseError;
  ReleaseAsset asset;           // 本体 (.dylib / .dll / .so)
  ReleaseAsset sha256Asset;     // 対応する .sha256

  QString name(const QString& lang) const {
    return (lang == QLatin1String("ja") && !nameJa.isEmpty()) ? nameJa : nameEn;
  }
  QString description(const QString& lang) const {
    return (lang == QLatin1String("ja") && !descriptionJa.isEmpty()) ? descriptionJa
                                                                   : descriptionEn;
  }
};

// 公式プラグイン一覧 (仕様は SPEC.md「プラグインのインストール」)。
//
//   1. マニフェストを https://farman.mashsoft.co.jp/plugins/manifest.json から取る。
//      取れなければ同梱の :/plugins/manifest.json にフォールバックする。
//   2. 各プラグインの最新リリースを GitHub Releases API から取り、このプラットフォーム用の
//      アセット (`<Name>-vX.Y.Z-{macos-arm64|windows-x64|linux-x86_64}.{dylib|dll|so}`) と
//      その .sha256 を選ぶ。
//
// 未認証の GitHub API は 60 req/h なので、結果は 1 時間キャッシュする (プロセス内 +
// キャッシュディレクトリのファイル。導入 → 再起動 → 一覧を開き直す、を繰り返しても
// API を叩き直さない)。取得は一覧を開いたとき (refresh) だけで、起動時には行わない。
class PluginCatalog : public QObject {
  Q_OBJECT

public:
  static PluginCatalog& instance();

  // 一覧を更新する。キャッシュが新しければ (force でなければ) ネットワークに出ずに
  // すぐ updated() を出す。進行中なら何もしない。
  void refresh(bool force = false);
  bool isRefreshing() const { return m_pending > 0 || m_manifestReply; }

  // 公式プラグインの一覧。まだ一度も更新していなければ、同梱のマニフェストだけを
  // 読んだ状態 (最新リリースは Unknown) を返す。ネットワークには出ない。
  // どのプラグインが公式かは、これで更新前から分かる。
  QList<PluginCatalogEntry> entries();
  // マニフェストを Web から取れたか (false = 同梱版にフォールバックした)。
  bool manifestFromNetwork() const { return m_manifestFromNetwork; }
  QDateTime fetchedAt() const { return m_fetchedAt; }

  // このプラットフォームの配布物の末尾 (例 "-macos-arm64.dylib")。未対応なら空。
  static QString assetSuffixForThisPlatform();
  // マニフェスト JSON をパースする (テスト用に公開)。
  static QList<PluginCatalogEntry> parseManifest(const QByteArray& json);
  // GitHub の release JSON から、このプラットフォーム用のアセットを選んで entry に詰める。
  static void applyReleaseJson(const QByteArray& json, const QString& assetSuffix,
                               PluginCatalogEntry* entry);

signals:
  // 一覧 (entries) が更新された。取得に失敗した項目は releaseState で分かる。
  void updated();

private:
  explicit PluginCatalog(QObject* parent = nullptr);

  void fetchManifest();
  void onManifestFinished();
  void fetchReleases();
  void onReleaseFinished(QNetworkReply* reply, const QString& id);
  void finishRefresh();

  bool loadCache();
  void saveCache() const;
  static QString cacheFilePath();

  QNetworkAccessManager* m_nam = nullptr;
  QNetworkReply*         m_manifestReply = nullptr;
  int                    m_pending = 0;   // 進行中の release 取得数
  QList<PluginCatalogEntry> m_entries;
  bool                   m_manifestFromNetwork = false;
  QDateTime              m_fetchedAt;
};

} // namespace Farman
