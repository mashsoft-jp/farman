#include <QApplication>
#include <QCommandLineParser>
#include <QDialogButtonBox>
#include <QDir>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLocale>
#include <QOpenGLVertexArrayObject>
#include <QProxyStyle>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTranslator>
#include "ui/MainWindow.h"
#include "viewer/ViewerDispatcher.h"
#include "core/ArchiveDispatcher.h"
#include "utils/ArchivePath.h"
#include "settings/Settings.h"

#ifdef FARMAN_HAVE_HEIF
#include <QtPlugin>
// libheif ベースの HEIF/HEIC 画像プラグイン (src/imageio)。静的リンクなので
// ここで明示的に取り込むと QImageReader のレジストリに登録される。
Q_IMPORT_PLUGIN(HeifPlugin)
#endif

namespace {

// ダイアログのボタン配置を全 OS で統一するためのスタイル。QDialogButtonBox は
// 既定で OS ネイティブのボタン順 (Windows は OK が左、macOS は OK が右) に従う
// が、farman では「OK を右端」(macOS / GNOME 風、既存の Tab 順 Cancel→Apply→OK
// とも一致) に固定する。
class FarmanProxyStyle : public QProxyStyle {
public:
  using QProxyStyle::QProxyStyle;
  int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                const QWidget* widget = nullptr,
                QStyleHintReturn* returnData = nullptr) const override {
    if (hint == QStyle::SH_DialogButtonLayout) {
      return QDialogButtonBox::MacLayout;
    }
#ifdef Q_OS_MAC
    // macOS ではコンボボックスのポップアップが既定で「メニュー形式」で描画され、
    // 項目テキストの縦位置がずれて日本語などが上側で見切れる。ポップアップを
    // 「リスト形式」に切り替えると、項目テキストがデリゲートで正しく縦センタ
    // リングされ見切れない (コンボのみに効き、通常メニューには影響しない)。
    if (hint == QStyle::SH_ComboBox_Popup) {
      return 0;
    }
#endif
    return QProxyStyle::styleHint(hint, option, widget, returnData);
  }

#ifdef Q_OS_MAC
  QSize sizeFromContents(ContentsType type, const QStyleOption* option,
                         const QSize& contentsSize,
                         const QWidget* widget = nullptr) const override {
    QSize sz = QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
    // macOS の QMacStyle は push ボタンの高さが標準値 (既定ボタンと同じ) の
    // ときだけ角丸 (pill) ベゼルを描き、1px でも高いと四角い bevel ベゼルに
    // フォールバックする。farman のフォント下では非既定ボタンの高さが 33px に
    // なり (既定ボタンは 32px)、同じダイアログ内でベゼルが不揃いになる。
    // 既定ボタン相当の高さに揃えて、全 push ボタンを同じベゼルに統一する。
    if (type == CT_PushButton) {
      // 漢字を含むボタン (例: "移動" "編集" "削除") は CJK フォールバック
      // フォントの影響でテキスト高さが 17px になり、QMacStyle が 32px の
      // 角丸(pill)ベゼルを使えず 33px の四角ベゼルにフォールバックしてしまう
      // (カタカナ "コピー" 等は 15px で 32px のまま)。content 高さがごく小さい
      // ときの結果 = pill 高さ (32px) を基準に、それを上限としてクランプし、
      // 全 push ボタンを pill ベゼルに揃える。
      const int pillH =
        QProxyStyle::sizeFromContents(CT_PushButton, option,
                                      QSize(contentsSize.width(), 0),
                                      widget).height();
      if (pillH > 0 && sz.height() > pillH) sz.setHeight(pillH);
    }
    return sz;
  }
#endif
};

// Qt 標準ボタン (OK / Cancel / Apply など) の文言は本来 Qt 同梱の翻訳
// (qt_<lang>.qm) が供給するが、Windows 配布は windeployqt --no-translations の
// ため未同梱で英語のままになる。farman 自前のカタログ (リソース埋め込みで全 OS
// ロードされる) に QPlatformTheme 文脈の訳を持たせ、どの OS でも同じ表記になる
// ようにする。ここでの QT_TRANSLATE_NOOP は lupdate にソース文字列を拾わせる
// ためだけのもので、実際の変換は QDialogButtonBox 側の translate() が行う。
[[maybe_unused]] static const char* const kStandardButtonTexts[] = {
  QT_TRANSLATE_NOOP("QPlatformTheme", "OK"),
  QT_TRANSLATE_NOOP("QPlatformTheme", "Cancel"),
  QT_TRANSLATE_NOOP("QPlatformTheme", "Apply"),
  QT_TRANSLATE_NOOP("QPlatformTheme", "Close"),
  QT_TRANSLATE_NOOP("QPlatformTheme", "Reset"),
  QT_TRANSLATE_NOOP("QPlatformTheme", "Restore Defaults"),
  QT_TRANSLATE_NOOP("QPlatformTheme", "Save"),
  QT_TRANSLATE_NOOP("QPlatformTheme", "Discard"),
  QT_TRANSLATE_NOOP("QPlatformTheme", "Open"),
  QT_TRANSLATE_NOOP("QPlatformTheme", "Help"),
  QT_TRANSLATE_NOOP("QPlatformTheme", "&Yes"),
  QT_TRANSLATE_NOOP("QPlatformTheme", "&No"),
};

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

  // 外部 3D ビュアープラグイン (farman-plugin-3d) はロード時に Qt6OpenGL を必要と
  // する。farman 本体は Qt6OpenGL を直接使わないため、CMake でリンクするだけでは
  // Windows の MSVC リンカが「未使用依存」として import を落とし、windeployqt が
  // Qt6OpenGL.dll を同梱しない (macOS/Linux は同梱される)。ここで Qt6OpenGL の
  // シンボルを 1 つ実参照して依存を確実に残し、配布物へ同梱させる。
  {
    volatile const void* keepQtOpenGL = &QOpenGLVertexArrayObject::staticMetaObject;
    Q_UNUSED(keepQtOpenGL);
  }

  // この実行ファイルが「本体」であることを Settings に伝える。以降、本体の
  // Settings だけが applyThemeFields で qApp パレット/フォントを適用する
  // (プラグイン dylib の Settings は qApp を触らない)。Settings::instance() を
  // 初めて使う前に呼ぶ必要がある。
  Farman::Settings::setHostApplication();

  app.setOrganizationName("Farman");
  app.setApplicationName("farman");
  app.setApplicationVersion(QStringLiteral(QT_STRINGIFY(FARMAN_VERSION)));

  // ダイアログのボタン配置を全 OS で統一する (OK を右端)。既定のスタイルを
  // ラップするので、ボタン順以外のネイティブな見た目はそのまま維持される。
  app.setStyle(new FarmanProxyStyle);

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

  // ビュアー基盤を初期化。同梱公式ビュアープラグイン登録後にユーザー指定ディレクトリ
  // から外部 IViewerPlugin (.dylib / .dll / .so) を読み込む。
  Farman::PluginContext pluginCtx;
  pluginCtx.farmanVersion = QStringLiteral(QT_STRINGIFY(FARMAN_VERSION));
  pluginCtx.appearance = Farman::ViewerDispatcher::currentAppearance();
  Farman::ViewerDispatcher::instance().setContext(pluginCtx);
  Farman::ViewerDispatcher::instance().registerBundledPlugins();
  // 外部プラグインディレクトリ。種別ごとのサブフォルダ (viewers/ / archives/)
  // から読み込む。構成をあらかじめ作成しておく: 無いと設定ダイアログの
  // 「ディレクトリを選ぶ」が既定位置を開けず別の場所 (作業ディレクトリ等) に
  // フォールバックし、ユーザーが置き場所を誤解する (実際に Windows で
  // Program Files 側と誤認する事例があった)。
  const QString pluginsDir = Farman::Settings::instance().pluginsDirectory();
  const QString extPluginsRoot =
    pluginsDir.isEmpty() ? Farman::Settings::defaultPluginsDirectory()
                         : pluginsDir;
  QDir().mkpath(extPluginsRoot + QStringLiteral("/viewers"));
  QDir().mkpath(extPluginsRoot + QStringLiteral("/archives"));
  Farman::ViewerDispatcher::instance().loadPlugins(
    QDir(extPluginsRoot + QStringLiteral("/viewers")));

  QObject::connect(&Farman::Settings::instance(), &Farman::Settings::settingsChanged,
                   &app, [] {
    Farman::ViewerDispatcher::instance().notifyAppearanceChanged(
      Farman::ViewerDispatcher::currentAppearance());
  });

  // アーカイブ (非ビュアー) プラグイン基盤を初期化。ビュアーと同じ外部プラグイン
  // ディレクトリ配下の archives/ サブディレクトリから IArchivePlugin を読み込み、
  // 名乗る拡張子 (例 .lzh) を ArchivePath へ登録して、その拡張子のアーカイブ
  // ブラウジング / 展開を有効化する。
  Farman::ArchivePluginContext archiveCtx;
  archiveCtx.farmanVersion = QStringLiteral(QT_STRINGIFY(FARMAN_VERSION));
  Farman::ArchiveDispatcher::instance().setContext(archiveCtx);
  Farman::ArchiveDispatcher::instance().registerBundledPlugins();
  Farman::ArchiveDispatcher::instance().loadPlugins(
    QDir(extPluginsRoot + QStringLiteral("/archives")));
  for (const QString& dotExt :
       Farman::ArchiveDispatcher::instance().registeredDotExtensions()) {
    Farman::ArchivePath::registerArchiveExtension(dotExt);
  }

  int exitCode = 0;
  {
    // MainWindow はこのスコープで破棄する。プラグイン生成の QWidget
    // (Inline ビュー / External ウィンドウ) は MainWindow 配下にあるため、
    // shutdownPlugins() による dylib アンロードより先に破棄されている
    // 必要がある (アンロード後に vtable / デストラクタへ飛ぶと UB)。
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

    exitCode = app.exec();

    // deleteLater() 済みのプラグインビューなど、保留中の破棄イベントを
    // MainWindow 破棄前に流しておく。
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }

  // プラグイン契約: アンロード前に shutdown() を 1 回呼ぶ。MainWindow の
  // 破棄後・QApplication の生存中というこの位置が安全なタイミング。
  Farman::ViewerDispatcher::instance().shutdownPlugins();
  Farman::ArchiveDispatcher::instance().shutdownPlugins();

  return exitCode;
}
