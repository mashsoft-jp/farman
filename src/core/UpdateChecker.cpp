#include "UpdateChecker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSysInfo>

namespace Farman {

namespace {

constexpr const char* kReleasesUrl =
  "https://api.github.com/repos/ms-haraki/farman/releases/latest";

// "0.9.0-test" を ([0, 9, 0], "test") のような (数値リスト, prerelease) に分解する。
// 不正フォーマットは数値リスト空で返す。
struct ParsedVersion {
  QList<int>  parts;       // [0, 9, 0] や [1, 0, 0]
  QString     prerelease;  // "test" / "beta.1" / 空文字 (release)
};

ParsedVersion parseVersion(const QString& v) {
  ParsedVersion pv;
  QString s = v.trimmed();
  if (s.startsWith(QLatin1Char('v')) || s.startsWith(QLatin1Char('V'))) {
    s = s.mid(1);
  }
  // "-" 以降が prerelease (semver 風)
  const int dash = s.indexOf(QLatin1Char('-'));
  QString numeric = (dash >= 0) ? s.left(dash) : s;
  if (dash >= 0) pv.prerelease = s.mid(dash + 1);

  const QStringList chunks = numeric.split(QLatin1Char('.'), Qt::SkipEmptyParts);
  for (const QString& c : chunks) {
    bool ok = false;
    const int n = c.toInt(&ok);
    if (!ok) {
      pv.parts.clear();
      break;
    }
    pv.parts.append(n);
  }
  return pv;
}

} // anonymous namespace

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent) {
  m_nam = new QNetworkAccessManager(this);
}

UpdateChecker::~UpdateChecker() = default;

void UpdateChecker::checkLatest() {
  if (m_inflight) return;
  // dev build (0.0.0 系) は静かにスキップ。GitHub API への無駄なアクセスを
  // 避け、開発機での通知ポップアップ抑制も兼ねる。
  const QString cur = currentVersion();
  const ParsedVersion pv = parseVersion(cur);
  if (pv.parts.isEmpty() || (pv.parts.size() >= 3
        && pv.parts[0] == 0 && pv.parts[1] == 0 && pv.parts[2] == 0)) {
    emit finished(false, ReleaseInfo{}, false,
                  QStringLiteral("skipped: dev build (%1)").arg(cur));
    return;
  }

  QNetworkRequest req{QUrl(QString::fromLatin1(kReleasesUrl))};
  req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
  // GitHub API は Accept: application/vnd.github+json を推奨。
  req.setRawHeader("Accept", "application/vnd.github+json");
  // タイムアウト (Qt 6 にあり)。長すぎると起動が重くなるので短め。
  req.setTransferTimeout(15000);

  m_inflight = true;
  m_reply = m_nam->get(req);
  connect(m_reply, &QNetworkReply::finished,
          this,    &UpdateChecker::onReplyFinished);
}

void UpdateChecker::onReplyFinished() {
  m_inflight = false;
  if (!m_reply) return;
  QNetworkReply* reply = m_reply;
  m_reply = nullptr;
  reply->deleteLater();

  // HTTP ステータスは error 状態でも attribute から取れる。404 は「stable release
  // がまだ無い」(draft / prerelease しか無い場合の GitHub /releases/latest の
  // 標準応答) として専用 reason を返す。
  const int status =
    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (reply->error() != QNetworkReply::NoError) {
    if (status == 404) {
      emit finished(false, ReleaseInfo{}, false,
                    QStringLiteral("no_published_release"));
      return;
    }
    emit finished(false, ReleaseInfo{}, false,
                  QStringLiteral("network error: %1").arg(reply->errorString()));
    return;
  }
  if (status == 404) {
    emit finished(false, ReleaseInfo{}, false,
                  QStringLiteral("no_published_release"));
    return;
  }
  if (status < 200 || status >= 300) {
    emit finished(false, ReleaseInfo{}, false,
                  QStringLiteral("HTTP %1").arg(status));
    return;
  }

  const QByteArray body = reply->readAll();
  ReleaseInfo info;
  QString err;
  if (!parseReleaseJson(body, &info, &err)) {
    emit finished(false, ReleaseInfo{}, false,
                  QStringLiteral("parse error: %1").arg(err));
    return;
  }
  // draft / prerelease は対象外 (SPEC.md の channel="stable" 方針)。
  if (info.draft || info.prerelease) {
    emit finished(false, ReleaseInfo{}, false,
                  QStringLiteral("latest is draft/prerelease (%1)").arg(info.tagName));
    return;
  }
  const bool isNewer = (compareVersions(currentVersion(), info.version) < 0);
  emit finished(true, info, isNewer, QString());
}

bool UpdateChecker::parseReleaseJson(const QByteArray& json, ReleaseInfo* out,
                                      QString* errorReason) {
  QJsonParseError pe{};
  const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
  if (pe.error != QJsonParseError::NoError) {
    if (errorReason) *errorReason = pe.errorString();
    return false;
  }
  if (!doc.isObject()) {
    if (errorReason) *errorReason = QStringLiteral("not an object");
    return false;
  }
  const QJsonObject root = doc.object();
  out->tagName    = root.value(QStringLiteral("tag_name")).toString();
  out->name       = root.value(QStringLiteral("name")).toString();
  out->body       = root.value(QStringLiteral("body")).toString();
  out->htmlUrl    = root.value(QStringLiteral("html_url")).toString();
  out->prerelease = root.value(QStringLiteral("prerelease")).toBool(false);
  out->draft      = root.value(QStringLiteral("draft")).toBool(false);
  // version は tag_name から "v" を剥がす
  out->version = out->tagName;
  if (out->version.startsWith(QLatin1Char('v')) ||
      out->version.startsWith(QLatin1Char('V'))) {
    out->version = out->version.mid(1);
  }
  // assets[] をパース
  const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
  for (const QJsonValue& v : assets) {
    if (!v.isObject()) continue;
    const QJsonObject ao = v.toObject();
    ReleaseAsset a;
    a.name        = ao.value(QStringLiteral("name")).toString();
    a.downloadUrl = ao.value(QStringLiteral("browser_download_url")).toString();
    a.sizeBytes   = static_cast<qint64>(ao.value(QStringLiteral("size")).toDouble());
    if (!a.name.isEmpty() && !a.downloadUrl.isEmpty()) {
      out->assets.append(a);
    }
  }
  if (out->tagName.isEmpty()) {
    if (errorReason) *errorReason = QStringLiteral("missing tag_name");
    return false;
  }
  return true;
}

int UpdateChecker::compareVersions(const QString& a, const QString& b) {
  const ParsedVersion pa = parseVersion(a);
  const ParsedVersion pb = parseVersion(b);

  if (pa.parts.isEmpty() && pb.parts.isEmpty()) return 0;
  if (pa.parts.isEmpty()) return -1;
  if (pb.parts.isEmpty()) return  1;

  // 数値部分を順に比較
  const int n = qMax(pa.parts.size(), pb.parts.size());
  for (int i = 0; i < n; ++i) {
    const int va = (i < pa.parts.size()) ? pa.parts[i] : 0;
    const int vb = (i < pb.parts.size()) ? pb.parts[i] : 0;
    if (va < vb) return -1;
    if (va > vb) return  1;
  }
  // 数値同じ → prerelease 比較 (semver: 「prerelease 付き < prerelease 無し」)
  const bool ap = !pa.prerelease.isEmpty();
  const bool bp = !pb.prerelease.isEmpty();
  if (ap && !bp) return -1;
  if (!ap && bp) return  1;
  if (ap && bp) return pa.prerelease.compare(pb.prerelease);
  return 0;
}

QString UpdateChecker::currentVersion() {
#ifdef FARMAN_VERSION
  return QStringLiteral(QT_STRINGIFY(FARMAN_VERSION));
#else
  return QString();
#endif
}

QString UpdateChecker::userAgent() {
  // 例: "farman/0.9.0 macos/arm64"
  const QString os = QSysInfo::productType();      // "macos" / "windows" / "ubuntu" 等
  const QString arch = QSysInfo::currentCpuArchitecture();  // "arm64" / "x86_64" 等
  return QStringLiteral("farman/%1 %2/%3")
    .arg(currentVersion(), os, arch);
}

} // namespace Farman
