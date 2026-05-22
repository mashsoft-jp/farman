#include "UpdateDownloader.h"

#include "Logger.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QCoreApplication>

namespace Farman {

namespace {

constexpr const char* kUserAgent = "farman-updater";

// 本体ファイルを置く一時ディレクトリ。
QString updateCacheDir() {
  const QString base =
    QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  return QDir(base).filePath(QStringLiteral("updates"));
}

// アーキテクチャ判定 (assets 名のマッチに使う)。
QString currentArchKey() {
  const QString a = QSysInfo::currentCpuArchitecture();
  if (a == QLatin1String("arm64")) return QStringLiteral("arm64");
  if (a == QLatin1String("x86_64")) return QStringLiteral("x86_64");
  return a;
}

} // anonymous namespace

UpdateDownloader::UpdateDownloader(QObject* parent) : QObject(parent) {
  m_nam = new QNetworkAccessManager(this);
}

UpdateDownloader::~UpdateDownloader() = default;

QString UpdateDownloader::assetPatternForThisPlatform() {
#if defined(Q_OS_MACOS)
  // 現状の release.yml は macOS = arm64 限定で DMG を出している。
  return QStringLiteral("macos-%1.dmg").arg(currentArchKey());
#elif defined(Q_OS_WIN)
  // Windows は Inno Setup の setup.exe。
  // release.yml は Microsoft の慣例に従って x86_64 ではなく `x64` 短縮表記で
  // アセット名を作っているので、currentArchKey() ("x86_64") をそのまま使うと
  // ミスマッチする。Windows x64 を明示的にハードコードする。
  // (将来 Windows on ARM64 をサポートする場合は arch 別に分岐させる)
  return QStringLiteral("windows-x64-setup.exe");
#elif defined(Q_OS_LINUX)
  // Linux は AppImage (.deb もあるが、自動更新では AppImage を優先)
  return QStringLiteral("linux-%1.AppImage").arg(currentArchKey());
#else
  return QString();
#endif
}

void UpdateDownloader::start(const ReleaseInfo& release) {
  if (m_state != State::Idle) return;

  // アセット選択: assets[] の中で、現プラットフォーム用パターンを末尾に含む
  // ものを 1 件選ぶ。複数候補があれば最初の 1 件。
  const QString pattern = assetPatternForThisPlatform();
  if (pattern.isEmpty()) {
    emit finished(false,
      QStringLiteral("Unsupported platform for auto-update"));
    return;
  }
  m_asset = {};
  m_sha256Asset = {};
  for (const ReleaseAsset& a : release.assets) {
    if (a.name.endsWith(pattern, Qt::CaseInsensitive)) {
      m_asset = a;
      break;
    }
  }
  if (m_asset.name.isEmpty()) {
    emit finished(false,
      QStringLiteral("No matching asset found for %1").arg(pattern));
    return;
  }
  // 対応する .sha256 (任意)
  const QString sha256Name = m_asset.name + QStringLiteral(".sha256");
  for (const ReleaseAsset& a : release.assets) {
    if (a.name == sha256Name) {
      m_sha256Asset = a;
      break;
    }
  }

  // 保存先を準備
  QDir().mkpath(updateCacheDir());
  m_savePath        = QDir(updateCacheDir()).filePath(m_asset.name);
  m_downloadTmpPath = m_savePath + QStringLiteral(".part");
  m_receivedBytes = 0;
  m_totalBytes    = m_asset.sizeBytes;

  if (m_sha256Asset.downloadUrl.isEmpty()) {
    // SHA256 アセット無し → 検証スキップでそのままダウンロードへ。
    m_expectedSha256.clear();
    beginDownload();
  } else {
    fetchSha256();
  }
}

void UpdateDownloader::fetchSha256() {
  m_state = State::FetchingSha256;
  emit phaseChanged(tr("Verifying signature..."));
  QNetworkRequest req{QUrl(m_sha256Asset.downloadUrl)};
  req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
  req.setTransferTimeout(15000);
  m_reply = m_nam->get(req);
  connect(m_reply, &QNetworkReply::finished, this,
          &UpdateDownloader::onSha256Finished);
}

void UpdateDownloader::onSha256Finished() {
  if (!m_reply) return;
  QNetworkReply* reply = m_reply;
  m_reply = nullptr;
  reply->deleteLater();
  if (reply->error() != QNetworkReply::NoError) {
    // SHA256 取得失敗は致命的ではない (.sha256 が release に無いケースもあるので
    // 検証スキップで本体ダウンロードに進む)。
    m_expectedSha256.clear();
  } else {
    // 内容は通常 "<hex> <filename>\n" 形式。最初のトークンを 16 進と仮定。
    const QString body = QString::fromUtf8(reply->readAll()).trimmed();
    const int sep = body.indexOf(QRegularExpression(QStringLiteral("\\s")));
    m_expectedSha256 = (sep > 0 ? body.left(sep) : body).toLower();
  }
  beginDownload();
}

void UpdateDownloader::beginDownload() {
  m_state = State::Downloading;
  emit phaseChanged(tr("Downloading update..."));

  QNetworkRequest req{QUrl(m_asset.downloadUrl)};
  req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
  // 大きい (数十 MB) ことが多いので timeout は長めに
  req.setTransferTimeout(0);  // 無効化 (進捗で死活判定する)

  m_reply = m_nam->get(req);
  // ストリームを .part に流す
  // 既存があれば一旦削除
  QFile::remove(m_downloadTmpPath);

  connect(m_reply, &QNetworkReply::readyRead, this,
          &UpdateDownloader::onAssetReadyRead);
  connect(m_reply, &QNetworkReply::downloadProgress, this,
          &UpdateDownloader::onAssetDownloadProgress);
  connect(m_reply, &QNetworkReply::finished, this,
          &UpdateDownloader::onAssetFinished);
}

void UpdateDownloader::onAssetReadyRead() {
  if (!m_reply) return;
  // append mode で書き出し
  QFile out(m_downloadTmpPath);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Append)) return;
  out.write(m_reply->readAll());
  out.close();
}

void UpdateDownloader::onAssetDownloadProgress(qint64 received, qint64 total) {
  m_receivedBytes = received;
  if (total > 0) m_totalBytes = total;
  emit progress(received, m_totalBytes);
}

void UpdateDownloader::onAssetFinished() {
  if (!m_reply) return;
  QNetworkReply* reply = m_reply;
  m_reply = nullptr;

  if (reply->error() != QNetworkReply::NoError) {
    const QString err = reply->errorString();
    reply->deleteLater();
    QFile::remove(m_downloadTmpPath);
    m_state = State::Idle;
    emit finished(false,
      QStringLiteral("Download failed: %1").arg(err));
    return;
  }
  // 残りバッファ flush
  QFile out(m_downloadTmpPath);
  if (out.open(QIODevice::WriteOnly | QIODevice::Append)) {
    out.write(reply->readAll());
    out.close();
  }
  reply->deleteLater();

  // .part → 本ファイル名に rename (atomic on same FS)
  QFile::remove(m_savePath);
  if (!QFile::rename(m_downloadTmpPath, m_savePath)) {
    m_state = State::Idle;
    emit finished(false,
      QStringLiteral("Could not save downloaded file to %1").arg(m_savePath));
    return;
  }

  // SHA256 検証
  m_state = State::Verifying;
  emit phaseChanged(tr("Verifying download..."));
  if (!verifySha256()) {
    QFile::remove(m_savePath);
    m_state = State::Idle;
    emit finished(false, QStringLiteral("SHA256 verification failed"));
    return;
  }

  if (!m_installEnabled) {
    // テスト用パス: 保存して終わるだけ
    m_state = State::Idle;
    emit finished(true, QString());
    return;
  }

  // インストール起動
  m_state = State::Installing;
  emit phaseChanged(tr("Launching installer..."));
  runInstall();
}

bool UpdateDownloader::verifySha256() {
  if (m_expectedSha256.isEmpty()) {
    // .sha256 が無いリリース → 検証はスキップ。注意ログを出すのは呼出側で。
    return true;
  }
  QFile f(m_savePath);
  if (!f.open(QIODevice::ReadOnly)) return false;
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&f)) return false;
  const QString got = hash.result().toHex().toLower();
  return got == m_expectedSha256;
}

void UpdateDownloader::runInstall() {
  // プラットフォーム別の install 経路。実体は別関数に切り出した方が読みやすい
  // が、まずは一直線で書く。すべての分岐で「正常 → 自プロセス終了 を呼出側に
  // 委ねる (finished(true) を emit)」、「失敗 → finished(false, reason)」。

#if defined(Q_OS_MACOS)
  // DMG を mount → /Volumes/farman/farman.app を /Applications/ にコピー →
  // unmount → 自プロセス終了。
  // ヘルパースクリプトを QProcess::startDetached で起動し、本体は終了する。
  // /Applications への書込みは管理者権限が必要なケースもあるが、ユーザー
  // 領域 (~/Applications) は無権限で書ける。まず /Applications にチャレンジ
  // して落ちたらホームに fallback。
  // シェルスクリプトを worktree 外に書き出して bash で実行する形に。
  const QString dmgPath = m_savePath;
  const QString script = QStringLiteral(R"(#!/bin/bash
set -e
DMG="%1"
# 自分自身 (farman プロセス) が終了するのを待ってからインストール開始。
# Mac は実行中の .app を上書きすると pid 不整合になるため。
sleep 2
MOUNT=$(/usr/bin/hdiutil attach -nobrowse -noverify -noautoopen "$DMG" | tail -1 | awk '{print $3}')
if [ -z "$MOUNT" ]; then echo "mount failed"; exit 1; fi
APP_SRC="$MOUNT/farman.app"
APP_DST_SYSTEM="/Applications/farman.app"
APP_DST_USER="$HOME/Applications/farman.app"
if [ -w /Applications ] || [ ! -e /Applications ]; then
  rm -rf "$APP_DST_SYSTEM"
  /bin/cp -R "$APP_SRC" "$APP_DST_SYSTEM"
  INSTALLED="$APP_DST_SYSTEM"
else
  mkdir -p "$HOME/Applications"
  rm -rf "$APP_DST_USER"
  /bin/cp -R "$APP_SRC" "$APP_DST_USER"
  INSTALLED="$APP_DST_USER"
fi
/usr/bin/hdiutil detach "$MOUNT" -quiet || true
# 新版を起動 (任意)
/usr/bin/open "$INSTALLED"
)").arg(dmgPath);

  const QString scriptPath = updateCacheDir() + QStringLiteral("/install-mac.sh");
  QFile sf(scriptPath);
  if (!sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    m_state = State::Idle;
    emit finished(false,
      QStringLiteral("Could not write install script to %1").arg(scriptPath));
    return;
  }
  sf.write(script.toUtf8());
  sf.close();
  QFile::setPermissions(scriptPath,
    QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
    QFile::ReadGroup | QFile::ExeGroup |
    QFile::ReadOther | QFile::ExeOther);

  if (!QProcess::startDetached(QStringLiteral("/bin/bash"),
                                { scriptPath })) {
    m_state = State::Idle;
    emit finished(false, QStringLiteral("Failed to launch install script"));
    return;
  }
  m_state = State::Idle;
  emit finished(true, QString());

#elif defined(Q_OS_WIN)
  // Windows: Inno Setup の setup.exe を起動するだけ。インストーラ側で
  // 既存 farman プロセスを終了させる責任を持つか (or ユーザー手動)、
  // farman 側で先に exit する。ここでは startDetached で投げて呼出側に
  // 終了を任せる。
  if (!QProcess::startDetached(m_savePath, {})) {
    m_state = State::Idle;
    emit finished(false, QStringLiteral("Failed to launch installer"));
    return;
  }
  m_state = State::Idle;
  emit finished(true, QString());

#elif defined(Q_OS_LINUX)
  // Linux AppImage: 現在の AppImage パスを取得して新版で置換 → 自プロセス終了
  // 後に新版を nohup で起動。
  //
  // 重要: AppImage は実行時に /tmp/.mount_xxx/ にマウントされ、その中の
  // usr/bin/farman が走るため QCoreApplication::applicationFilePath() は
  // マウント先内部の "/tmp/.mount_xxx/usr/bin/farman" を返す。これを mv で
  // 上書きしてもユーザーが本来持っている .AppImage ファイルは変わらない。
  // AppImage ランタイムは本物の AppImage パスを `$APPIMAGE` 環境変数で
  // 教えてくれるので、まずそれを優先する。
  // (AppImage 以外の経路 (例: .deb で /opt/farman/ に展開) で起動された
  //  場合は $APPIMAGE が無いので applicationFilePath() にフォールバック。
  //  ただし deb 経由のアップデートは sudo が要るので失敗する見込み。
  //  その場合スクリプトのログを /tmp/farman-update.log に残してユーザーが
  //  手動対処できるようにする。)
  QString currentApp = qEnvironmentVariable("APPIMAGE");
  if (currentApp.isEmpty()) {
    currentApp = QCoreApplication::applicationFilePath();
  }
  const QString script = QStringLiteral(R"(#!/bin/bash
exec >/tmp/farman-update.log 2>&1
echo "[$(date)] farman update: NEW=%1 TARGET=%2"
NEW="%1"
TARGET="%2"
sleep 2
if [ ! -f "$NEW" ]; then
  echo "ERROR: new AppImage not found at $NEW"
  exit 1
fi
chmod +x "$NEW" || { echo "ERROR: chmod on NEW failed"; exit 1; }
if ! mv -f "$NEW" "$TARGET"; then
  echo "ERROR: mv -f \"$NEW\" \"$TARGET\" failed (permission? not an AppImage?)"
  exit 1
fi
chmod +x "$TARGET"
echo "OK: replaced $TARGET, relaunching"
nohup "$TARGET" >/dev/null 2>&1 &
)").arg(m_savePath, currentApp);

  const QString scriptPath = updateCacheDir() + QStringLiteral("/install-linux.sh");
  QFile sf(scriptPath);
  if (!sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    m_state = State::Idle;
    emit finished(false,
      QStringLiteral("Could not write install script to %1").arg(scriptPath));
    return;
  }
  sf.write(script.toUtf8());
  sf.close();
  QFile::setPermissions(scriptPath,
    QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
    QFile::ReadGroup | QFile::ExeGroup |
    QFile::ReadOther | QFile::ExeOther);

  Logger::instance().info(
    QStringLiteral("Linux update: NEW=%1, TARGET=%2 (APPIMAGE=%3)")
      .arg(m_savePath, currentApp,
           qEnvironmentVariable("APPIMAGE", QStringLiteral("<unset>"))));

  if (!QProcess::startDetached(QStringLiteral("/bin/bash"), { scriptPath })) {
    m_state = State::Idle;
    emit finished(false, QStringLiteral("Failed to launch install script"));
    return;
  }
  m_state = State::Idle;
  emit finished(true, QString());

#else
  m_state = State::Idle;
  emit finished(false, QStringLiteral("Unsupported platform for install"));
#endif
}

} // namespace Farman
