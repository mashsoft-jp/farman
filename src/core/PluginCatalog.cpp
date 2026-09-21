#include "PluginCatalog.h"

#include "Logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QSysInfo>

#include <algorithm>
#include <utility>

namespace Farman {

namespace {

constexpr const char* kManifestUrl = "https://farman.mashsoft.co.jp/plugins/manifest.json";
constexpr const char* kBundledManifest = ":/plugins/manifest.json";
constexpr const char* kReleaseUrlFormat = "https://api.github.com/repos/%1/releases/latest";
constexpr int kCacheMinutes = 60;
constexpr int kSupportedFormatVersion = 1;

QString userAgent() {
  return QStringLiteral("farman/%1 %2/%3")
    .arg(PluginInstaller::hostVersion(), QSysInfo::productType(),
         QSysInfo::currentCpuArchitecture());
}

// "owner/repo" 形式だけを受け付ける (URL に埋め込むので、それ以外は弾く)。
bool isValidRepo(const QString& repo) {
  const QStringList parts = repo.split(QLatin1Char('/'));
  if (parts.size() != 2) return false;
  for (const QString& part : parts) {
    if (part.isEmpty()) return false;
    for (const QChar c : part) {
      if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_')
          && c != QLatin1Char('.')) {
        return false;
      }
    }
  }
  return true;
}

QJsonObject assetToJson(const ReleaseAsset& asset) {
  QJsonObject obj;
  obj.insert(QStringLiteral("name"), asset.name);
  obj.insert(QStringLiteral("url"), asset.downloadUrl);
  obj.insert(QStringLiteral("size"), static_cast<double>(asset.sizeBytes));
  return obj;
}

ReleaseAsset assetFromJson(const QJsonObject& obj) {
  ReleaseAsset asset;
  asset.name        = obj.value(QStringLiteral("name")).toString();
  asset.downloadUrl = obj.value(QStringLiteral("url")).toString();
  asset.sizeBytes   = static_cast<qint64>(obj.value(QStringLiteral("size")).toDouble());
  return asset;
}

} // namespace

PluginCatalog& PluginCatalog::instance() {
  static PluginCatalog inst;
  return inst;
}

PluginCatalog::PluginCatalog(QObject* parent) : QObject(parent) {
  m_nam = new QNetworkAccessManager(this);
}

QString PluginCatalog::assetSuffixForThisPlatform() {
  // 公式プラグインの release.yml と Web サイト (docs/plugins.js) の命名に合わせる。
  const QString arch = QSysInfo::currentCpuArchitecture();
#if defined(Q_OS_MACOS)
  return arch == QLatin1String("arm64") ? QStringLiteral("-macos-arm64.dylib") : QString();
#elif defined(Q_OS_WIN)
  return arch == QLatin1String("x86_64") ? QStringLiteral("-windows-x64.dll") : QString();
#elif defined(Q_OS_LINUX)
  return arch == QLatin1String("x86_64") ? QStringLiteral("-linux-x86_64.so") : QString();
#else
  return QString();
#endif
}

QList<PluginCatalogEntry> PluginCatalog::parseManifest(const QByteArray& json) {
  QList<PluginCatalogEntry> entries;
  const QJsonObject root = QJsonDocument::fromJson(json).object();
  if (root.value(QStringLiteral("formatVersion")).toInt() != kSupportedFormatVersion) {
    return entries;
  }
  const QJsonArray plugins = root.value(QStringLiteral("plugins")).toArray();
  for (const QJsonValue& v : plugins) {
    const QJsonObject obj = v.toObject();
    PluginCatalogEntry entry;
    entry.id       = obj.value(QStringLiteral("id")).toString();
    entry.repo     = obj.value(QStringLiteral("repo")).toString();
    entry.fileName = obj.value(QStringLiteral("fileName")).toString();
    entry.minFarmanVersion = obj.value(QStringLiteral("minFarmanVersion")).toString();
    const QString kind = obj.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("viewer")) {
      entry.kind = PluginInstaller::Kind::Viewer;
    } else if (kind == QLatin1String("archive")) {
      entry.kind = PluginInstaller::Kind::Archive;
    }
    const QJsonObject name = obj.value(QStringLiteral("name")).toObject();
    entry.nameJa = name.value(QStringLiteral("ja")).toString();
    entry.nameEn = name.value(QStringLiteral("en")).toString();
    const QJsonObject description = obj.value(QStringLiteral("description")).toObject();
    entry.descriptionJa = description.value(QStringLiteral("ja")).toString();
    entry.descriptionEn = description.value(QStringLiteral("en")).toString();

    // 必須項目が欠けたもの・未知の種別・不正な repo は一覧に出さない。
    if (entry.id.isEmpty() || entry.fileName.isEmpty() || entry.nameEn.isEmpty()
        || entry.kind == PluginInstaller::Kind::Unknown || !isValidRepo(entry.repo)) {
      continue;
    }
    entries.append(entry);
  }
  return entries;
}

void PluginCatalog::applyReleaseJson(const QByteArray& json, const QString& assetSuffix,
                                     PluginCatalogEntry* entry) {
  const QJsonObject root = QJsonDocument::fromJson(json).object();
  QString tag = root.value(QStringLiteral("tag_name")).toString();
  if (tag.isEmpty()) {
    entry->releaseState = PluginCatalogEntry::ReleaseState::Failed;
    entry->releaseError = QStringLiteral("unexpected response");
    return;
  }
  if (tag.startsWith(QLatin1Char('v')) || tag.startsWith(QLatin1Char('V'))) {
    tag = tag.mid(1);
  }
  entry->latestVersion = tag;
  entry->releaseUrl    = root.value(QStringLiteral("html_url")).toString();
  entry->asset         = {};
  entry->sha256Asset   = {};

  QList<ReleaseAsset> assets;
  const QJsonArray array = root.value(QStringLiteral("assets")).toArray();
  for (const QJsonValue& v : array) {
    const QJsonObject obj = v.toObject();
    ReleaseAsset asset;
    asset.name        = obj.value(QStringLiteral("name")).toString();
    asset.downloadUrl = obj.value(QStringLiteral("browser_download_url")).toString();
    asset.sizeBytes   = static_cast<qint64>(obj.value(QStringLiteral("size")).toDouble());
    assets.append(asset);
  }

  // 本体は「<fileName>-...<suffix>」。基底名がマニフェストの fileName と一致するものに
  // 限る (同じリリースに別の配布物が混ざっていても拾わない)。
  const QString wantedBase = PluginInstaller::pluginBaseName(entry->fileName);
  if (!assetSuffix.isEmpty()) {
    for (const ReleaseAsset& asset : assets) {
      if (asset.name.endsWith(assetSuffix, Qt::CaseInsensitive)
          && PluginInstaller::pluginBaseName(asset.name) == wantedBase) {
        entry->asset = asset;
        break;
      }
    }
  }
  if (!entry->asset.name.isEmpty()) {
    const QString sha256Name = entry->asset.name + QStringLiteral(".sha256");
    for (const ReleaseAsset& asset : assets) {
      if (asset.name == sha256Name) {
        entry->sha256Asset = asset;
        break;
      }
    }
  }
  // .sha256 が無い配布物は照合できないので導入対象にしない (公式配布物は必須)。
  // ダウンロード元は、マニフェストの repo のリリース配布物 (https) に限る。
  const QString urlPrefix =
    QStringLiteral("https://github.com/%1/releases/download/").arg(entry->repo);
  const bool usable = entry->asset.downloadUrl.startsWith(urlPrefix)
                   && entry->sha256Asset.downloadUrl.startsWith(urlPrefix);
  entry->releaseState = usable ? PluginCatalogEntry::ReleaseState::Ok
                               : PluginCatalogEntry::ReleaseState::NoAssetForPlatform;
  entry->releaseError.clear();
}

QList<PluginCatalogEntry> PluginCatalog::entries() {
  if (m_entries.isEmpty()) {
    QFile bundled(QString::fromLatin1(kBundledManifest));
    if (bundled.open(QIODevice::ReadOnly)) {
      m_entries = parseManifest(bundled.readAll());
    }
  }
  return m_entries;
}

void PluginCatalog::refresh(bool force) {
  if (isRefreshing()) return;

  if (!force) {
    const bool memoryFresh = m_fetchedAt.isValid()
      && m_fetchedAt.secsTo(QDateTime::currentDateTimeUtc()) < kCacheMinutes * 60;
    if (memoryFresh || loadCache()) {
      emit updated();
      return;
    }
  }
  fetchManifest();
}

void PluginCatalog::fetchManifest() {
  QNetworkRequest req{QUrl(QString::fromLatin1(kManifestUrl))};
  req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
  req.setTransferTimeout(15000);
  m_manifestReply = m_nam->get(req);
  connect(m_manifestReply, &QNetworkReply::finished, this,
          &PluginCatalog::onManifestFinished);
}

void PluginCatalog::onManifestFinished() {
  QNetworkReply* reply = m_manifestReply;
  m_manifestReply = nullptr;
  if (!reply) return;
  reply->deleteLater();

  QList<PluginCatalogEntry> entries;
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
    entries = parseManifest(reply->readAll());
  }
  m_manifestFromNetwork = !entries.isEmpty();
  if (entries.isEmpty()) {
    // オフライン / サイトに未反映 / 形式が新しすぎる → 同梱版で続ける。
    Logger::instance().info(
      QStringLiteral("Plugins: official plugin list not available online (%1); "
                     "using the bundled list")
        .arg(reply->error() == QNetworkReply::NoError
               ? QStringLiteral("HTTP %1").arg(status) : reply->errorString()));
    QFile bundled(QString::fromLatin1(kBundledManifest));
    if (bundled.open(QIODevice::ReadOnly)) {
      entries = parseManifest(bundled.readAll());
    }
  }
  m_entries = entries;
  fetchReleases();
}

void PluginCatalog::fetchReleases() {
  if (m_entries.isEmpty()) {
    finishRefresh();
    return;
  }
  m_pending = m_entries.size();
  for (const PluginCatalogEntry& entry : std::as_const(m_entries)) {
    QNetworkRequest req{QUrl(QString::fromLatin1(kReleaseUrlFormat).arg(entry.repo))};
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(15000);
    QNetworkReply* reply = m_nam->get(req);
    const QString id = entry.id;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, id]() { onReleaseFinished(reply, id); });
  }
}

void PluginCatalog::onReleaseFinished(QNetworkReply* reply, const QString& id) {
  reply->deleteLater();
  for (PluginCatalogEntry& entry : m_entries) {
    if (entry.id != id) continue;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
      applyReleaseJson(reply->readAll(), assetSuffixForThisPlatform(), &entry);
    } else {
      entry.releaseState = PluginCatalogEntry::ReleaseState::Failed;
      if (status == 403 || status == 429) {
        entry.releaseError = QStringLiteral("GitHub API rate limit");
      } else if (status == 404) {
        entry.releaseError = QStringLiteral("no published release");
      } else {
        entry.releaseError = reply->errorString();
      }
      Logger::instance().warn(
        QStringLiteral("Plugins: could not get the latest release of %1 (%2)")
          .arg(entry.repo, entry.releaseError));
    }
  }
  if (--m_pending <= 0) {
    m_pending = 0;
    finishRefresh();
  }
}

void PluginCatalog::finishRefresh() {
  // 1 件も取得できなかった結果はキャッシュしない (次に開いたとき取り直す)。
  const bool anyOk = std::any_of(m_entries.cbegin(), m_entries.cend(),
    [](const PluginCatalogEntry& e) {
      return e.releaseState != PluginCatalogEntry::ReleaseState::Failed
          && e.releaseState != PluginCatalogEntry::ReleaseState::Unknown;
    });
  if (anyOk) {
    m_fetchedAt = QDateTime::currentDateTimeUtc();
    saveCache();
  } else {
    m_fetchedAt = QDateTime();
  }
  emit updated();
}

QString PluginCatalog::cacheFilePath() {
  const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  return QDir(base).filePath(QStringLiteral("plugins/catalog.json"));
}

bool PluginCatalog::loadCache() {
  QFile file(cacheFilePath());
  if (!file.open(QIODevice::ReadOnly)) return false;
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  const QDateTime fetchedAt = QDateTime::fromString(
    root.value(QStringLiteral("fetchedAt")).toString(), Qt::ISODate);
  if (!fetchedAt.isValid()
      || fetchedAt.secsTo(QDateTime::currentDateTimeUtc()) >= kCacheMinutes * 60
      || fetchedAt > QDateTime::currentDateTimeUtc()
      || root.value(QStringLiteral("assetSuffix")).toString() != assetSuffixForThisPlatform()) {
    return false;
  }

  QJsonObject manifest;
  manifest.insert(QStringLiteral("formatVersion"), kSupportedFormatVersion);
  manifest.insert(QStringLiteral("plugins"), root.value(QStringLiteral("plugins")));
  QList<PluginCatalogEntry> entries =
    parseManifest(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
  if (entries.isEmpty()) return false;

  const QJsonObject releases = root.value(QStringLiteral("releases")).toObject();
  for (PluginCatalogEntry& entry : entries) {
    const QJsonObject rel = releases.value(entry.id).toObject();
    entry.releaseState = static_cast<PluginCatalogEntry::ReleaseState>(
      rel.value(QStringLiteral("state")).toInt());
    entry.latestVersion = rel.value(QStringLiteral("version")).toString();
    entry.releaseUrl    = rel.value(QStringLiteral("url")).toString();
    entry.releaseError  = rel.value(QStringLiteral("error")).toString();
    entry.asset         = assetFromJson(rel.value(QStringLiteral("asset")).toObject());
    entry.sha256Asset   = assetFromJson(rel.value(QStringLiteral("sha256")).toObject());
  }
  // 取得に失敗した項目を含むキャッシュは使わず、取り直す。
  const bool anyFailed = std::any_of(entries.cbegin(), entries.cend(),
    [](const PluginCatalogEntry& e) {
      return e.releaseState == PluginCatalogEntry::ReleaseState::Failed
          || e.releaseState == PluginCatalogEntry::ReleaseState::Unknown;
    });
  if (anyFailed) return false;

  m_entries = entries;
  m_fetchedAt = fetchedAt;
  m_manifestFromNetwork = root.value(QStringLiteral("manifestFromNetwork")).toBool();
  return true;
}

void PluginCatalog::saveCache() const {
  QJsonArray plugins;
  QJsonObject releases;
  for (const PluginCatalogEntry& entry : m_entries) {
    QJsonObject name;
    name.insert(QStringLiteral("ja"), entry.nameJa);
    name.insert(QStringLiteral("en"), entry.nameEn);
    QJsonObject description;
    description.insert(QStringLiteral("ja"), entry.descriptionJa);
    description.insert(QStringLiteral("en"), entry.descriptionEn);
    QJsonObject plugin;
    plugin.insert(QStringLiteral("id"), entry.id);
    plugin.insert(QStringLiteral("repo"), entry.repo);
    plugin.insert(QStringLiteral("kind"),
                  entry.kind == PluginInstaller::Kind::Viewer ? QStringLiteral("viewer")
                                                              : QStringLiteral("archive"));
    plugin.insert(QStringLiteral("fileName"), entry.fileName);
    plugin.insert(QStringLiteral("minFarmanVersion"), entry.minFarmanVersion);
    plugin.insert(QStringLiteral("name"), name);
    plugin.insert(QStringLiteral("description"), description);
    plugins.append(plugin);

    QJsonObject rel;
    rel.insert(QStringLiteral("state"), static_cast<int>(entry.releaseState));
    rel.insert(QStringLiteral("version"), entry.latestVersion);
    rel.insert(QStringLiteral("url"), entry.releaseUrl);
    rel.insert(QStringLiteral("error"), entry.releaseError);
    rel.insert(QStringLiteral("asset"), assetToJson(entry.asset));
    rel.insert(QStringLiteral("sha256"), assetToJson(entry.sha256Asset));
    releases.insert(entry.id, rel);
  }
  QJsonObject root;
  root.insert(QStringLiteral("fetchedAt"), m_fetchedAt.toString(Qt::ISODate));
  root.insert(QStringLiteral("assetSuffix"), assetSuffixForThisPlatform());
  root.insert(QStringLiteral("manifestFromNetwork"), m_manifestFromNetwork);
  root.insert(QStringLiteral("plugins"), plugins);
  root.insert(QStringLiteral("releases"), releases);

  const QString path = cacheFilePath();
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  }
}

} // namespace Farman
