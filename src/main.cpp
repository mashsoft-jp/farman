#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLocale>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTranslator>
#include "ui/MainWindow.h"
#include "viewer/ViewerDispatcher.h"
#include "settings/Settings.h"

namespace {
// 二重起動検出用のサーバー名。HOME や AppConfigLocation のハッシュを混ぜて
// 同一ユーザーの異なるセッション (例: SSH 経由の別ホスト) で衝突しないように
// する。マルチユーザー環境でも同じ socket 名を取り合わないように、
// ローカル限定の AppConfigLocation を使う。
QString singleInstanceServerName() {
  QString id = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  if (id.isEmpty()) id = QDir::homePath();
  return QStringLiteral("farman-instance-%1")
    .arg(QString::number(qHash(id), 16));
}
}  // namespace

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  app.setOrganizationName("Farman");
  app.setApplicationName("farman");
  app.setApplicationVersion(QStringLiteral(QT_STRINGIFY(FARMAN_VERSION)));

  // --version / --help を処理して即終了する (GUI を立ち上げない)。
  // process() は該当オプションが付いていたら stdout に出力して exit() する。
  // それ以外 (引数なし通常起動) はそのまま返ってきて GUI 初期化へ進む。
  QCommandLineParser parser;
  parser.setApplicationDescription(
    QStringLiteral("Qt6-based dual-pane file manager"));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.process(app);

  // macOS では既定で Tab がテキスト系ウィジェットしかフォーカスしない
  // (TabFocusTextControls)。farman ではツールバーのトグルボタン / コンボなど
  // 全コントロールを Tab で巡れるように、StyleHints レベルで TabFocusAllControls
  // を強制する。各ウィジェットの setFocusPolicy(Qt::StrongFocus) と組み合わせて
  // 初めて Tab が機能するため、両方が必要。
  QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);

  // ランタイム用アイコン (タスクバー / ウィンドウタイトル / Linux WM など)。
  // macOS では setWindowIcon を呼ばない: Qt は macOS でも setWindowIcon の
  // 結果で Dock アイコンを runtime 上書きしてしまうため、CMake で指定した
  // MACOSX_BUNDLE_ICON_FILE (= icon.icns、Apple HIG の角丸版) が
  // 後から塗りつぶされてしまう。Dock もウィンドウタイトル左の小アイコンも
  // Bundle 経由の icon.icns に任せた方が macOS では自然な見た目になる。
#ifndef Q_OS_MACOS
  app.setWindowIcon(QIcon(QStringLiteral(":/icons/farman.png")));
#endif

  // Settings をロードして UI 言語を決定する。
  // QTranslator は QApplication と同じ寿命にしたいので static にしておく。
  Farman::Settings::instance().load();

  // 二重起動チェック (Settings の singleInstance ON のとき)。
  // - 既存インスタンスがあれば QLocalSocket 経由で activate を投げて即終了。
  // - 無ければ QLocalServer を立ち上げて、後続の起動からの activate を受ける。
  static QLocalServer* singleInstanceServer = nullptr;
  if (Farman::Settings::instance().singleInstance()) {
    const QString serverName = singleInstanceServerName();
    {
      QLocalSocket probe;
      probe.connectToServer(serverName);
      if (probe.waitForConnected(300)) {
        // 既存インスタンス検出。activate 要求を送って自分は終了する。
        probe.write("activate\n");
        probe.flush();
        probe.waitForBytesWritten(300);
        probe.disconnectFromServer();
        return 0;
      }
    }
    // 自分が最初のインスタンス。残骸 socket を消してから listen 開始。
    QLocalServer::removeServer(serverName);
    singleInstanceServer = new QLocalServer(&app);
    if (!singleInstanceServer->listen(serverName)) {
      // listen 失敗してもアプリ自体は続行 (single-instance 機能を諦める)。
      delete singleInstanceServer;
      singleInstanceServer = nullptr;
    }
  }
  static QTranslator appTranslator;
  static QTranslator qtTranslator;

  QString lang;
  switch (Farman::Settings::instance().language()) {
    case Farman::LanguageMode::English:  lang = "en"; break;
    case Farman::LanguageMode::Japanese: lang = "ja"; break;
    case Farman::LanguageMode::Auto:     lang = QLocale::system().name(); break;
  }
  // ja_JP なども ja として扱う
  const QString shortLang = lang.section('_', 0, 0);

  // Qt 標準ダイアログ (OK/Cancel など) の翻訳
  if (qtTranslator.load("qt_" + shortLang,
                        QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
    app.installTranslator(&qtTranslator);
  }

  // farman 自身の翻訳。リソース埋め込み済み (qt_add_translations のデフォルト命名)
  if (appTranslator.load(QStringLiteral(":/translations/farman_") + shortLang)) {
    app.installTranslator(&appTranslator);
  }

  // ビュアー基盤を初期化。組み込みビュアー登録後にユーザー指定ディレクトリ
  // から外部 IViewerPlugin (.dylib / .dll / .so) を読み込む。
  Farman::PluginContext pluginCtx;
  pluginCtx.farmanVersion = QStringLiteral(QT_STRINGIFY(FARMAN_VERSION));
  Farman::ViewerDispatcher::instance().setContext(pluginCtx);
  Farman::ViewerDispatcher::instance().registerBuiltins();
  const QString pluginsDir = Farman::Settings::instance().pluginsDirectory();
  Farman::ViewerDispatcher::instance().loadPlugins(
    QDir(pluginsDir.isEmpty()
           ? Farman::Settings::defaultPluginsDirectory()
           : pluginsDir));

  Farman::MainWindow window;
  window.show();

  // 後続インスタンスからの activate 要求でこのウィンドウを前面に出す。
  if (singleInstanceServer) {
    QObject::connect(singleInstanceServer, &QLocalServer::newConnection,
      [&window]() {
        QLocalSocket* client = singleInstanceServer->nextPendingConnection();
        if (!client) return;
        QObject::connect(client, &QLocalSocket::readyRead, &window,
          [client, &window]() {
            const QByteArray msg = client->readAll();
            if (msg.contains("activate")) {
              if (window.isMinimized()) window.showNormal();
              window.show();
              window.raise();
              window.activateWindow();
            }
          });
        QObject::connect(client, &QLocalSocket::disconnected,
                         client, &QObject::deleteLater);
      });
  }

  return app.exec();
}
