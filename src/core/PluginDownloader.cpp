#include "PluginDownloader.h"

#include "Logger.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>

namespace Farman {

namespace {

constexpr const char* kUserAgent = "farman-plugin-installer";

QString downloadDir() {
  const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  return QDir(base).filePath(QStringLiteral("plugins/downloads"));
}

QNetworkRequest makeRequest(const QString& url, int timeoutMs) {
  QNetworkRequest req{QUrl(url)};
  req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
  // GitHub のリリース配布物は objects.githubusercontent.com へリダイレクトされる。
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
  req.setTransferTimeout(timeoutMs);
  return req;
}

} // namespace

PluginDownloader::PluginDownloader(QObject* parent) : QObject(parent) {
  m_nam = new QNetworkAccessManager(this);
}

PluginDownloader::~PluginDownloader() {
  cancel();
}

void PluginDownloader::start(const PluginCatalogEntry& entry) {
  if (m_state != State::Idle) return;
  m_entry = entry;
  m_expectedSha256.clear();

  const QString urlPrefix =
    QStringLiteral("https://github.com/%1/releases/download/").arg(entry.repo);
  if (entry.releaseState != PluginCatalogEntry::ReleaseState::Ok
      || !entry.asset.downloadUrl.startsWith(urlPrefix)
      || !entry.sha256Asset.downloadUrl.startsWith(urlPrefix)
      || entry.asset.name.contains(QLatin1Char('/'))
      || entry.asset.name.contains(QLatin1Char('\\'))) {
    fail(tr("No downloadable file for this platform."));
    return;
  }

  QDir().mkpath(downloadDir());
  m_savePath = QDir(downloadDir()).filePath(entry.asset.name);
  m_partPath = m_savePath + QStringLiteral(".part");

  m_state = State::FetchingSha256;
  m_reply = m_nam->get(makeRequest(entry.sha256Asset.downloadUrl, 15000));
  connect(m_reply, &QNetworkReply::finished, this, &PluginDownloader::onSha256Finished);
}

void PluginDownloader::cancel() {
  if (m_state == State::Idle) return;
  if (m_reply) {
    m_reply->disconnect(this);
    m_reply->abort();
    m_reply->deleteLater();
    m_reply = nullptr;
  }
  cleanup();
}

void PluginDownloader::onSha256Finished() {
  QNetworkReply* reply = m_reply;
  m_reply = nullptr;
  if (!reply) return;
  reply->deleteLater();

  if (reply->error() != QNetworkReply::NoError) {
    fail(tr("Could not download the checksum (%1).").arg(reply->errorString()));
    return;
  }
  // 内容は "<hex>  <filename>\n" 形式。最初のトークンが SHA256 (64 桁の 16 進)。
  const QString body = QString::fromUtf8(reply->readAll()).trimmed();
  const QString token =
    body.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0).toLower();
  static const QRegularExpression hex64(QStringLiteral("^[0-9a-f]{64}$"));
  if (!hex64.match(token).hasMatch()) {
    fail(tr("The checksum file is not valid."));
    return;
  }
  m_expectedSha256 = token;
  beginDownload();
}

void PluginDownloader::beginDownload() {
  QFile::remove(m_partPath);
  m_partFile = new QFile(m_partPath, this);
  if (!m_partFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    fail(tr("Could not save the downloaded file (%1).").arg(m_partPath));
    return;
  }

  m_state = State::Downloading;
  // 数 MB〜十数 MB。転送が止まったら 60 秒で打ち切る。
  m_reply = m_nam->get(makeRequest(m_entry.asset.downloadUrl, 60000));
  connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
    if (m_reply && m_partFile) {
      m_partFile->write(m_reply->readAll());
    }
  });
  connect(m_reply, &QNetworkReply::downloadProgress, this,
          [this](qint64 received, qint64 total) {
    emit progress(received, total > 0 ? total : m_entry.asset.sizeBytes);
  });
  connect(m_reply, &QNetworkReply::finished, this, &PluginDownloader::onAssetFinished);
}

void PluginDownloader::onAssetFinished() {
  QNetworkReply* reply = m_reply;
  m_reply = nullptr;
  if (!reply) return;
  reply->deleteLater();

  if (reply->error() != QNetworkReply::NoError) {
    fail(tr("Download failed (%1).").arg(reply->errorString()));
    return;
  }
  m_partFile->write(reply->readAll());
  m_partFile->close();

  // SHA256 を照合してから本来のファイル名にする (照合前のファイルを残さない)。
  QFile part(m_partPath);
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!part.open(QIODevice::ReadOnly) || !hash.addData(&part)) {
    fail(tr("Could not save the downloaded file (%1).").arg(m_partPath));
    return;
  }
  part.close();
  if (QString::fromLatin1(hash.result().toHex()) != m_expectedSha256) {
    Logger::instance().warn(
      QStringLiteral("Plugins: SHA256 mismatch for %1").arg(m_entry.asset.name));
    fail(tr("The downloaded file did not match its checksum (SHA256)."));
    return;
  }

  QFile::remove(m_savePath);
  if (!QFile::rename(m_partPath, m_savePath)) {
    fail(tr("Could not save the downloaded file (%1).").arg(m_savePath));
    return;
  }
  Logger::instance().info(
    QStringLiteral("Plugins: downloaded and verified %1").arg(m_entry.asset.name));
  const QString path = m_savePath;
  cleanup();
  emit finished(true, path, QString());
}

void PluginDownloader::fail(const QString& reason) {
  cleanup();
  emit finished(false, QString(), reason);
}

void PluginDownloader::cleanup() {
  if (m_partFile) {
    m_partFile->close();
    m_partFile->deleteLater();
    m_partFile = nullptr;
  }
  if (!m_partPath.isEmpty()) {
    QFile::remove(m_partPath);
  }
  m_state = State::Idle;
}

} // namespace Farman
