#pragma once

#include "types.h"
#include "ColorScheme.h"
#include "core/UserCommand.h"
#include <QObject>
#include <QDateTime>
#include <QFont>
#include <QColor>
#include <QMap>
#include <QSize>
#include <QPoint>
#include <QList>

namespace Farman {

// ブックマーク（お気に入りディレクトリ）1件
struct Bookmark {
  QString name;
  QString path;
  // 初回起動で自動注入されるデフォルト（Home/Documents 等）は true。
  // この印が付いたブックマークは削除禁止。Rename/Move は許可。
  bool    isDefault = false;
};

// ColorRule / CategoryColor は ColorScheme から参照する都合で types.h に
// 移動済み。ここでは types.h の定義をそのまま使う。

// ペインごとの設定
// バイナリビュアー設定のシリアライズ補助
int                binaryViewerUnitToBytes(BinaryViewerUnit unit);
BinaryViewerUnit   bytesToBinaryViewerUnit(int bytes);
QString            binaryViewerEndianToString(BinaryViewerEndian endian);
BinaryViewerEndian stringToBinaryViewerEndian(const QString& str);

struct PaneSettings {
  QString         path;
  SortKey         sortKey      = SortKey::Name;
  Qt::SortOrder   sortOrder    = Qt::AscendingOrder;
  SortKey         sortKey2nd   = SortKey::Name;
  SortDirsType    sortDirsType = SortDirsType::First;
  bool            sortDotFirst = true;
  Qt::CaseSensitivity sortCS   = Qt::CaseInsensitive;
  QStringList     nameFilters;
  AttrFilterFlags attrFilter   = AttrFilter::None;
};

class Settings : public QObject {
  Q_OBJECT

public:
  static Settings& instance();

  // ── ペイン設定 ─────────────────────────
  PaneSettings paneSettings(PaneType pane) const;
  void setPaneSettings(PaneType pane, const PaneSettings& s);

  // ── ディレクトリ単位のソート・フィルタ上書き ─
  // 絶対パスをキーに、そのディレクトリ専用の PaneSettings を保持する。
  // PaneSettings::path は未使用。
  bool         hasPathOverride(const QString& path) const;
  PaneSettings pathOverride(const QString& path)    const;
  void         setPathOverride(const QString& path, const PaneSettings& s);
  void         removePathOverride(const QString& path);

  // ── 表示設定 ───────────────────────────
  // ベース色 (= UI 全体の地色 / 文字色; QPalette 派生元)。
  // ダイアログ背景・ボタン・メニュー等、個別設定の無いウィジェットは
  // この 2 色から計算したパレットで描画される。
  QColor baseBackground()              const;
  QColor baseForeground()              const;
  void   setBaseBackground(const QColor& c);
  void   setBaseForeground(const QColor& c);

  // UI フォント (汎用ウィジェット用; QApplication::setFont() に渡される)。
  // ファイルリスト / アドレス / 各ビュアーは個別の font 設定を保持。
  QFont  uiFont()                      const;
  void   setUiFont(const QFont& font);

  // ファイルリスト用フォント (旧名 font())
  QFont  font()                        const;
  void   setFont(const QFont& font);

  // アドレスバー (各ペイン上部のパス/URI 表示) 用フォント
  QFont  addressFont()                 const;
  void   setAddressFont(const QFont& font);

  // ファイルリスト 1 行の縦幅 (px)。0 で「Auto」(Qt のデフォルト)。
  int    fileListRowHeight()           const;
  void   setFileListRowHeight(int px);

  // ファイルサイズと日時の表示形式は 2 画面モードと 1 画面モードで別々に
  // 持つ。表示中のモードに応じてどちらかが使われる。Single 側の既定値は
  // Dual 側と同じ (旧設定からの移行時もコピーされる)。
  FileSizeFormat fileSizeFormatDual()        const;
  void           setFileSizeFormatDual(FileSizeFormat fmt);
  FileSizeFormat fileSizeFormatSingle()      const;
  void           setFileSizeFormatSingle(FileSizeFormat fmt);

  // 桁区切りカンマを使うか (`Auto` 以外で有効)。Dual / Single でそれぞれ。デフォルト ON。
  bool           fileSizeThousandsSeparatorDual()   const;
  void           setFileSizeThousandsSeparatorDual(bool enabled);
  bool           fileSizeThousandsSeparatorSingle() const;
  void           setFileSizeThousandsSeparatorSingle(bool enabled);

  // ファイルリストの列表示 ON/OFF (Name 以外)。Dual / Single モードで別々。
  // 並び順は固定 (FileListModel::Column の並び)。
  struct ListColumnVisibility {
    bool type         = true;
    bool size         = true;
    bool lastModified = true;
    bool created      = false;
    bool permissions  = false;
    bool attributes   = false;
    bool owner        = false;
    bool group        = false;
    bool linkTarget   = false;
  };
  ListColumnVisibility listColumnVisibilityDual()   const;
  void                 setListColumnVisibilityDual(const ListColumnVisibility& v);
  ListColumnVisibility listColumnVisibilitySingle() const;
  void                 setListColumnVisibilitySingle(const ListColumnVisibility& v);

  QString        dateTimeFormatDual()        const;
  void           setDateTimeFormatDual(const QString& fmt);
  QString        dateTimeFormatSingle()      const;
  void           setDateTimeFormatSingle(const QString& fmt);

  // ── カラーリング ───────────────────────
  QList<ColorRule> colorRules()         const;
  void             setColorRules(const QList<ColorRule>& rules);

  // カテゴリ別カラー（Normal / Hidden / Directory）。
  // selected=true で選択時、inactive=true で非アクティブペイン側のスタイルを取得・設定する。
  CategoryColor    categoryColor(FileCategory cat, bool selected = false, bool inactive = false) const;
  void             setCategoryColor(FileCategory cat, bool selected, bool inactive, const CategoryColor& c);

  // 非アクティブペインの専用カラーを使用するか
  bool             useInactivePaneColors() const;
  void             setUseInactivePaneColors(bool use);

  // ペイン上部のアドレスバーのカラー
  QColor           addressForeground()              const;
  void             setAddressForeground(const QColor& c);
  QColor           addressBackground()              const;
  void             setAddressBackground(const QColor& c);
  // アーカイブ内ブラウジング中のアドレスバー色 (通常 FS と視覚区別)
  QColor           archiveAddressForeground()       const;
  void             setArchiveAddressForeground(const QColor& c);
  QColor           archiveAddressBackground()       const;
  void             setArchiveAddressBackground(const QColor& c);

  // ディレクトリ比較の差分着色 (Differ / OnlyHere)
  QColor           compareDifferForeground()        const;
  void             setCompareDifferForeground(const QColor& c);
  QColor           compareDifferBackground()        const;
  void             setCompareDifferBackground(const QColor& c);
  QColor           compareOnlyHereForeground()      const;
  void             setCompareOnlyHereForeground(const QColor& c);
  QColor           compareOnlyHereBackground()      const;
  void             setCompareOnlyHereBackground(const QColor& c);

  // カーソル（現在行下線）のカラー
  QColor           cursorColor(bool active)         const;
  void             setCursorColor(bool active, const QColor& c);

  // カーソル形状 (Underline / RowBackground) と Underline 時の線太さ (px)
  CursorShape      cursorShape()                    const;
  void             setCursorShape(CursorShape shape);
  int              cursorThickness()                const;
  void             setCursorThickness(int px);

  // ── 起動設定 ───────────────────────────
  // ペインごとの初期表示ディレクトリ
  InitialPathMode initialPathMode(PaneType pane)            const;
  void            setInitialPathMode(PaneType pane, InitialPathMode mode);
  QString         customInitialPath(PaneType pane)          const;
  void            setCustomInitialPath(PaneType pane, const QString& path);

  bool confirmOnExit()                  const;
  void setConfirmOnExit(bool confirm);

  // 二重起動を禁止するか。ON のとき既に farman が動いていれば、新規プロセス
  // は起動せず既存ウィンドウを前面に出す。
  bool singleInstance()                 const;
  void setSingleInstance(bool enabled);

  // ビュアープラグインを探しに行くディレクトリ (空 = 既定のユーザー固有 path)。
  // ".dylib" / ".so" / ".dll" を起動時にロードする。
  // 既定: macOS = ~/Library/Application Support/farman/plugins,
  //       Linux = ~/.local/share/farman/plugins,
  //       Windows = %APPDATA%/Farman/farman/plugins
  // (QStandardPaths::AppDataLocation + "/plugins")。
  QString pluginsDirectory()            const;  // 空のときは defaultPluginsDirectory()
  void    setPluginsDirectory(const QString& dir);
  // 既定のプラグインディレクトリを返す (= 上記の OS 別 path)。
  // pluginsDirectory() が空のとき loadPlugins に使う実体。
  static QString defaultPluginsDirectory();

  // ビュアーの表示モード (Inline / External)。Inline はメインウィンドウ内の
  // ViewerPanel を使う現状の挙動、External はファイル毎に独立ウィンドウ。
  ViewerMode viewerMode()               const;
  void       setViewerMode(ViewerMode mode);

  // ペインの表示モード (List / Thumbnail)。Left/Right で独立に保持。
  // 起動時に各ペインへ復元され、Ctrl+G コマンドや View メニューから切替えると
  // 即座に保存される。デフォルトは List。
  ListViewMode paneViewMode(PaneType pane)               const;
  void         setPaneViewMode(PaneType pane, ListViewMode mode);

  // サムネイル表示モード時のサムネイル外接サイズ。ペインごとではなくグローバル
  // (左右で同じサイズ)。デフォルトは Medium。
  ThumbnailSize thumbnailSize()                          const;
  void          setThumbnailSize(ThumbnailSize size);

  // メニューバーの下に表示するツールバー (頻出操作のボタン群) の表示/非表示。
  // View メニューの "Toolbar" 項目および Settings → General からトグルできる。
  bool showToolbar()                    const;
  void setShowToolbar(bool show);

  // ツールバーのボタン表示スタイル (IconOnly / TextOnly / IconBesideText)。
  // Settings → General のコンボから切替。MainWindow::applyToolbarVisibility が
  // 反映する。デフォルトは IconOnly (省スペース)。
  ToolbarStyle toolbarStyle()           const;
  void         setToolbarStyle(ToolbarStyle style);

  // ファイルマネージャパネルのレイアウト (Dual / Single / Preview)。
  // View メニュー / ツールバー / Ctrl+O (Single) / Ctrl+P (Preview) で切替。
  // 起動時の復元に使うため永続化する。
  LayoutMode layoutMode() const;
  void       setLayoutMode(LayoutMode mode);

  // プレビューモードの動作パラメタ。
  // - debounceMs: カーソル変化からプレビュー切替までの待ち時間 (50〜1000)
  // - maxFileSizeBytes: これを超えるファイルはプレビューせず "Too large" 表示
  int     previewDebounceMs() const;
  void    setPreviewDebounceMs(int ms);
  qint64  previewMaxFileSizeBytes() const;
  void    setPreviewMaxFileSizeBytes(qint64 bytes);

  // 同期ブラウズが「追従先が存在しないため自動 OFF」した瞬間に通知
  // ダイアログを出すかどうか。ダイアログ側の「次回以降表示しない」
  // チェックを ON にした時にもこのフラグが false になる。デフォは true。
  bool syncBrowseShowDisabledDialog()        const;
  void setSyncBrowseShowDisabledDialog(bool show);

  // ── 自動アップデート ─────────────────────────
  // 起動時に最大 1 日 1 回 GitHub Releases をチェックして最新版があればダイアログ
  // で通知する。SPEC.md "自動アップデート" 節参照。
  bool      autoUpdateCheckOnStartup()         const;
  void      setAutoUpdateCheckOnStartup(bool on);
  bool      autoUpdateSilent()                 const;  // 確認なしで適用
  void      setAutoUpdateSilent(bool on);
  QDateTime autoUpdateLastCheckedAt()          const;
  void      setAutoUpdateLastCheckedAt(const QDateTime& at);
  QStringList autoUpdateSkippedVersions()      const;
  void        setAutoUpdateSkippedVersions(const QStringList& versions);
  void        addAutoUpdateSkippedVersion(const QString& version);
  QString   autoUpdateChannel()                const;  // 現状 "stable" 固定
  void      setAutoUpdateChannel(const QString& channel);

  // ── 言語設定 ─────────────────────────────
  // UI 言語。変更は次回起動時に反映される (実装上の制限、再起動を促す)。
  LanguageMode language()               const;
  void         setLanguage(LanguageMode lang);

  // ── ログ設定 ─────────────────────────────
  bool    logVisible()                  const;
  void    setLogVisible(bool visible);
  // 表示時のログペインの高さ (px)
  int     logPaneHeight()               const;
  void    setLogPaneHeight(int px);
  bool    logToFile()                   const;
  void    setLogToFile(bool toFile);
  // ログを保存するディレクトリ。日次ファイル `farman-YYYY-MM-DD.log` がこの中に作られる。
  // 既定: <AppConfigLocation>
  QString logDirectory()                const;
  void    setLogDirectory(const QString& dir);
  // ログファイルの保持日数。0 は永久保持。
  int     logRetentionDays()            const;
  void    setLogRetentionDays(int days);

  bool cursorLoop()                     const;
  void setCursorLoop(bool loop);

  // Shift+文字キーによる頭文字検索でドットファイル/ディレクトリの 2 文字目もマッチさせるか
  bool typeAheadIncludeDotfiles()       const;
  void setTypeAheadIncludeDotfiles(bool include);

  // ── テキストビュアー設定 ───────────────────
  // ファイルを開く際にテキストビュアーで処理する拡張子と MIME パターン。
  // MIME は末尾 `*` で前方一致 (例: "text/*")、それ以外は完全一致または inherits 判定。
  QStringList textViewerExtensions()    const;
  void        setTextViewerExtensions(const QStringList& exts);
  QStringList textViewerMimePatterns()  const;
  void        setTextViewerMimePatterns(const QStringList& patterns);
  QFont   textViewerFont()              const;
  void    setTextViewerFont(const QFont& font);
  QString textViewerEncoding()          const;
  void    setTextViewerEncoding(const QString& encoding);
  bool    textViewerShowLineNumbers()   const;
  void    setTextViewerShowLineNumbers(bool show);
  bool    textViewerWordWrap()          const;
  void    setTextViewerWordWrap(bool wrap);
  // 通常テキスト/選択範囲/行番号エリアの前景・背景色
  QColor  textViewerNormalForeground()  const;
  QColor  textViewerNormalBackground()  const;
  QColor  textViewerSelectedForeground()const;
  QColor  textViewerSelectedBackground()const;
  QColor  textViewerLineNumberForeground() const;
  QColor  textViewerLineNumberBackground() const;
  void    setTextViewerNormalForeground(const QColor& c);
  void    setTextViewerNormalBackground(const QColor& c);
  void    setTextViewerSelectedForeground(const QColor& c);
  void    setTextViewerSelectedBackground(const QColor& c);
  void    setTextViewerLineNumberForeground(const QColor& c);
  void    setTextViewerLineNumberBackground(const QColor& c);

  // ── Markdown ビュアー設定 ───────────────────────
  // ファイルを開く際に Markdown ビュアー (= 整形 HTML 表示) で処理する拡張子。
  // テキストビュアー判定より優先される。
  QStringList markdownViewerExtensions() const;
  void        setMarkdownViewerExtensions(const QStringList& exts);

  // ── PDF ビュアー設定 ───────────────────────
  // ファイルを開く際に PDF ビュアー (Qt PDF) で処理する拡張子。
  // 既定は "pdf" のみ。バイナリビュアー (拡張子非該当) より先に判定される。
  QStringList pdfViewerExtensions() const;
  void        setPdfViewerExtensions(const QStringList& exts);

  // ── CSV / TSV ビュアー設定 ───────────────────────
  // ファイルを開く際に CSV ビュアー (QTableView + 自前パーサ) で処理する拡張子。
  // 既定は "csv" / "tsv"。テキストビュアー判定より先に走らせる
  // (resolveAuto の順序で決定)。
  QStringList csvViewerExtensions() const;
  void        setCsvViewerExtensions(const QStringList& exts);

  // ── 画像ビュアー設定 ───────────────────────
  QStringList           imageViewerExtensions()        const;
  void                  setImageViewerExtensions(const QStringList& exts);
  QStringList           imageViewerMimePatterns()      const;
  void                  setImageViewerMimePatterns(const QStringList& patterns);
  int                   imageViewerZoomPercent()       const;
  void                  setImageViewerZoomPercent(int percent);
  bool                  imageViewerFitToWindow()       const;
  void                  setImageViewerFitToWindow(bool fit);
  bool                  imageViewerAnimation()         const;
  void                  setImageViewerAnimation(bool on);
  // 透明部分の表示モード (デフォルト) と各モードの色設定
  ImageTransparencyMode imageViewerTransparencyMode()  const;
  void                  setImageViewerTransparencyMode(ImageTransparencyMode mode);
  QColor                imageViewerSolidColor()        const;
  void                  setImageViewerSolidColor(const QColor& c);
  QColor                imageViewerCheckerColor1()     const;
  void                  setImageViewerCheckerColor1(const QColor& c);
  QColor                imageViewerCheckerColor2()     const;
  void                  setImageViewerCheckerColor2(const QColor& c);

  // ── バイナリビュアー設定 ───────────────────
  BinaryViewerUnit   binaryViewerUnit()     const;
  void               setBinaryViewerUnit(BinaryViewerUnit unit);
  BinaryViewerEndian binaryViewerEndian()   const;
  void               setBinaryViewerEndian(BinaryViewerEndian endian);
  QString            binaryViewerEncoding() const;
  void               setBinaryViewerEncoding(const QString& encoding);
  QFont              binaryViewerFont()     const;
  void               setBinaryViewerFont(const QFont& font);
  // 通常テキスト / 選択範囲 / アドレス列の前景・背景色
  QColor             binaryViewerNormalForeground()    const;
  QColor             binaryViewerNormalBackground()    const;
  QColor             binaryViewerSelectedForeground()  const;
  QColor             binaryViewerSelectedBackground()  const;
  QColor             binaryViewerAddressForeground()   const;
  QColor             binaryViewerAddressBackground()   const;
  void               setBinaryViewerNormalForeground(const QColor& c);
  void               setBinaryViewerNormalBackground(const QColor& c);
  void               setBinaryViewerSelectedForeground(const QColor& c);
  void               setBinaryViewerSelectedBackground(const QColor& c);
  void               setBinaryViewerAddressForeground(const QColor& c);
  void               setBinaryViewerAddressBackground(const QColor& c);

  // ディレクトリ履歴を終了時に保存し、起動時に復元するか
  bool persistHistory()                 const;
  void setPersistHistory(bool persist);

  // ペインごとの履歴エントリ（最新順）
  QStringList paneHistory(PaneType pane) const;
  void        setPaneHistory(PaneType pane, const QStringList& entries);

  // ── ブックマーク ─────────────────────────
  QList<Bookmark> bookmarks() const;
  void            setBookmarks(const QList<Bookmark>& list);

  // ── 外部アプリ連携 (UserCommand) ──────────
  // Tools メニュー / Settings → General タブの External Applications で扱う
  // 外部アプリ一覧。先頭 2 件は builtin=true の terminal / editor で固定
  // (UserCommandManager が並び順を保つ前提)。
  QList<UserCommand> userCommands() const;
  void               setUserCommands(const QList<UserCommand>& cmds);

  // コピー／移動の「自動リネーム」で使うサフィックステンプレート。
  // "{n}" がカウンタのプレースホルダ。未指定なら " ({n})" を補う。
  QString autoRenameTemplate()          const;
  void    setAutoRenameTemplate(const QString& tmpl);

  // 削除時にゴミ箱へ送るのが既定か（false なら完全削除が既定）。
  // 削除確認ダイアログで都度切替可能。
  bool    defaultDeleteToTrash()        const;
  void    setDefaultDeleteToTrash(bool toTrash);

  // 進捗ダイアログ (コピー/移動/削除) を完了時に自動で閉じるか。
  // ダイアログ内のチェックボックスから直接書き換えられる。デフォルト false。
  bool    progressAutoClose()           const;
  void    setProgressAutoClose(bool autoClose);

  // ファイル検索の除外ディレクトリ（ディレクトリ名の glob パターンのリスト）。
  // 検索ダイアログの初期値に使われ、ダイアログ上で一時的に編集できる。
  QStringList searchExcludeDirs()       const;
  void        setSearchExcludeDirs(const QStringList& patterns);

  // ── ウィンドウ設定 ──────────────────────
  WindowSizeMode     windowSizeMode()     const;
  void               setWindowSizeMode(WindowSizeMode mode);
  QSize              customWindowSize()   const;
  void               setCustomWindowSize(const QSize& size);
  QSize              lastWindowSize()     const;
  void               setLastWindowSize(const QSize& size);

  WindowPositionMode windowPositionMode() const;
  void               setWindowPositionMode(WindowPositionMode mode);
  QPoint             customWindowPosition() const;
  void               setCustomWindowPosition(const QPoint& pos);
  QPoint             lastWindowPosition() const;
  void               setLastWindowPosition(const QPoint& pos);

  // ── テーマ (Light / Dark スキーム) ────────────
  // モードと「現在実際に適用されているスキーム種別」を分けて取得する。
  // Auto モード時は OS のカラースキーム (Qt 6.5+) に追従する。
  ThemeMode  themeMode()        const;
  ThemeMode  effectiveTheme()   const;  // Light か Dark のどちらかが返る
  void       setThemeMode(ThemeMode mode);

  // 任意のスキーム (Light / Dark) を直接読み書きする。
  // (Theme インポートやスキーム間コピー用。アクティブスキームを書き換えた
  //  場合は m_ フィールドへの再ロードを内部で行い settingsChanged を発火)
  ColorScheme scheme(ThemeMode which) const;
  void        setScheme(ThemeMode which, const ColorScheme& s);

  // OS の現在のカラースキーム (Light/Dark) を読む。Settings に保存された
  // m_themeMode とは独立に「いま OS は何?」を取得したいときに使う。
  // Qt 6.5 未満や判定不能なら Light を返す。
  ThemeMode   detectOsTheme()  const;

  // ── 読み書き ───────────────────────────
  void load();
  void save() const;
  // 全フィールドを構築時のデフォルトに戻す。キーバインドは Settings の
  // 範囲外なので影響しない (KeyBindingManager 側に reset がある)。
  // 完了後に settingsChanged() を発火する。
  void resetToDefaults();

  // settings.json の canonical パス (= AppConfigLocation/settings.json)。
  // export / import / 診断ログ表示で表示するために公開する。
  static QString settingsFilePath();

  // ── マシン間移行用の "設定全体のエクスポート / インポート" ────────
  // settings.json をユーザー指定の場所にコピー / 上書きする。
  //
  // export:
  //   1. save() で現在の in-memory を settings.json に確定
  //   2. settings.json を path にコピー (既存上書き)
  //   true: 成功 / false: 失敗 (errorOut にメッセージ)。
  bool exportTo(const QString& path, QString* errorOut = nullptr) const;
  //
  // import:
  //   1. path を JSON として parse (= 形式チェック)
  //   2. version フィールドを最低限確認
  //   3. settings.json を上書き
  //   4. load() で in-memory を再読込
  //   5. settingsChanged() を emit
  //   呼出元 (Settings ダイアログ等) は emit 後に自分の UI を再構築する。
  bool importFrom(const QString& path, QString* errorOut = nullptr);

signals:
  void settingsChanged() const;

private:
  explicit Settings(QObject* parent = nullptr);
  // 全フィールドをコンストラクタ時点と同じ値に揃える。
  // resetToDefaults() の本体でもあり、コンストラクタからも呼ばれる。
  void applyDefaults();

  PaneSettings     m_paneSettings[static_cast<int>(PaneType::Count)];
  QMap<QString, PaneSettings> m_pathOverrides;
  // ベース色 + UI フォント (= 個別設定外の UI 全体の地色 / フォント)
  QColor           m_baseBackground       = QColor(Qt::white);
  QColor           m_baseForeground       = QColor(Qt::black);
  QFont            m_uiFont;
  QFont            m_font;
  QFont            m_addressFont;
  int              m_fileListRowHeight = 0;  // 0 = Auto
  // ファイルサイズ / 日時の表示形式 (2 画面 / 1 画面で別々)
  FileSizeFormat   m_fileSizeFormatDual    = FileSizeFormat::Auto;
  FileSizeFormat   m_fileSizeFormatSingle  = FileSizeFormat::Auto;
  bool             m_fileSizeThousandsSeparatorDual   = true;  // Auto 以外で適用
  bool             m_fileSizeThousandsSeparatorSingle = true;
  ListColumnVisibility m_listColumnsDual;
  ListColumnVisibility m_listColumnsSingle;
  QString          m_dateTimeFormatDual    = "yyyy/MM/dd HH:mm:ss";
  QString          m_dateTimeFormatSingle  = "yyyy/MM/dd HH:mm:ss";
  QList<ColorRule> m_colorRules;
  CategoryColor    m_categoryColors[static_cast<int>(FileCategory::Count)];
  CategoryColor    m_selectedCategoryColors[static_cast<int>(FileCategory::Count)];
  CategoryColor    m_inactiveCategoryColors[static_cast<int>(FileCategory::Count)];
  CategoryColor    m_inactiveSelectedCategoryColors[static_cast<int>(FileCategory::Count)];
  bool             m_useInactivePaneColors = false;

  // Path label & cursor colors
  QColor           m_addressForeground    = QColor(Qt::black);
  QColor           m_addressBackground    = QColor(0xE0, 0xE0, 0xE0);
  // アーカイブ内ブラウジング時のアドレスバー色
  QColor           m_archiveAddressForeground = QColor(Qt::black);
  QColor           m_archiveAddressBackground = QColor(0xFF, 0xE9, 0xA8);
  // ディレクトリ比較の着色 (Light の既定値)
  QColor           m_compareDifferForeground   = QColor(Qt::black);
  QColor           m_compareDifferBackground   = QColor(0xFF, 0xD8, 0xA8);
  QColor           m_compareOnlyHereForeground = QColor(Qt::black);
  QColor           m_compareOnlyHereBackground = QColor(0xC8, 0xE6, 0xB4);
  QColor           m_cursorActiveColor    = QColor(Qt::black);
  QColor           m_cursorInactiveColor  = QColor(Qt::lightGray);
  CursorShape      m_cursorShape          = CursorShape::Underline;
  int              m_cursorThickness      = 2;  // px (Underline 時のみ)
  // Per-pane 初期表示ディレクトリ（デフォルトは LastSession で従来動作と互換）
  InitialPathMode  m_initialPathMode[static_cast<int>(PaneType::Count)] = {
    InitialPathMode::LastSession,
    InitialPathMode::LastSession
  };
  QString          m_customInitialPath[static_cast<int>(PaneType::Count)];

  bool             m_confirmOnExit   = false;
  bool             m_singleInstance  = true;
  bool             m_syncBrowseShowDisabledDialog = true;
  // 自動アップデート関連 (SPEC.md "自動アップデート" 節参照)。
  bool             m_autoUpdateCheckOnStartup = true;
  bool             m_autoUpdateSilent          = false;
  QDateTime        m_autoUpdateLastCheckedAt;       // null = 未チェック
  QStringList      m_autoUpdateSkippedVersions;     // ["1.2.3", ...]
  QString          m_autoUpdateChannel = QStringLiteral("stable");
  // ビュアープラグインを置くディレクトリ。空文字 = defaultPluginsDirectory()。
  QString          m_pluginsDirectory;
  // ビュアー表示モード。デフォルトは Inline (ビュアーパネルでの表示)。
  ViewerMode       m_viewerMode      = ViewerMode::Inline;
  // ペインごとの表示モード (List / Thumbnail)。
  ListViewMode     m_paneViewMode[static_cast<int>(PaneType::Count)] = {
    ListViewMode::List, ListViewMode::List
  };
  // サムネイルサイズ (グローバル、両ペイン共通)。
  ThumbnailSize    m_thumbnailSize   = ThumbnailSize::Medium;
  // メニュー下のツールバーの表示。デフォルトは ON (新機能を見つけてもらう)。
  // 不要なら View → Toolbar / Settings → General からオフにできる。
  bool             m_showToolbar     = true;
  // ツールバーのボタン表示スタイル。デフォルトは IconOnly (省スペース)。
  ToolbarStyle     m_toolbarStyle    = ToolbarStyle::IconOnly;
  // ファイルマネージャパネルのレイアウト。Dual がデフォルト。Preview / Single
  // は永続化されるので、終了時に Preview なら次回も Preview で起動する。
  LayoutMode       m_layoutMode      = LayoutMode::Dual;
  int              m_previewDebounceMs = 200;            // 50〜1000
  qint64           m_previewMaxFileSizeBytes = 10LL * 1024 * 1024; // 10 MB
  LanguageMode     m_language         = LanguageMode::Auto;
  bool             m_logVisible       = true;
  int              m_logPaneHeight    = 120;  // px
  bool             m_logToFile        = true;
  QString          m_logDirectory;  // 既定はコンストラクタで AppConfigLocation
  int              m_logRetentionDays = 7;  // 0 = 永久保持
  bool             m_cursorLoop      = false;
  bool             m_typeAheadIncludeDotfiles = true;

  // Markdown viewer
  // .md / .markdown / .mdown / .mkd を整形 HTML で表示する。
  // textViewerExtensions より優先される (resolveAuto で Markdown 判定が先)。
  QStringList        m_markdownViewerExtensions = {
    "md", "markdown", "mdown", "mkd",
  };

  // PDF viewer (Qt PDF 経由)。バイナリ判定より先に評価する。
  QStringList        m_pdfViewerExtensions = {
    "pdf",
  };

  // CSV / TSV viewer (QTableView + 自前パーサ)。テキスト判定より先に評価。
  QStringList        m_csvViewerExtensions = {
    "csv", "tsv",
  };

  // Text viewer
  QStringList        m_textViewerExtensions   = {
    // テキスト系
    "txt", "log",
    // c/cc/cpp/cxx/cs/css/conf/cfg — c から始まるテキスト系全般
    // Java の class はバイナリなので除外、Windows 系バイナリ (cab/chm/com) も除外
    "c*", "!class", "!cab", "!chm", "!com",
    "h", "hpp",
    // 言語
    "py", "js", "ts", "java", "rs", "go", "rb", "php", "pl", "pm",
    // Web
    "htm*", "json", "xml",
    // シェル
    "*sh", "fish",
    // 設定
    "yml", "yaml", "toml", "ini"
  };
  QStringList        m_textViewerMimePatterns = { "text/*", "text/plain" };
  QFont              m_textViewerFont;  // 既定は monospace (コンストラクタで初期化)
  QString            m_textViewerEncoding         = QStringLiteral("Auto");
  bool               m_textViewerShowLineNumbers  = true;
  bool               m_textViewerWordWrap         = false;
  QColor             m_textViewerNormalFg         = QColor(Qt::black);
  QColor             m_textViewerNormalBg;
  QColor             m_textViewerSelectedFg       = QColor(Qt::white);
  QColor             m_textViewerSelectedBg       = QColor(0x31, 0x6A, 0xC5);
  QColor             m_textViewerLineNumberFg     = QColor(Qt::darkGray);
  QColor             m_textViewerLineNumberBg     = QColor(0xF0, 0xF0, 0xF0);

  // Image viewer
  QStringList           m_imageViewerExtensions   = {
    "png", "jp*g", "gif", "bmp", "svg", "webp", "ico", "tif*", "psd"
  };
  QStringList           m_imageViewerMimePatterns = { "image/*" };
  int                   m_imageViewerZoomPercent      = 100;
  bool                  m_imageViewerFitToWindow      = false;
  bool                  m_imageViewerAnimation        = false;
  ImageTransparencyMode m_imageViewerTransparencyMode = ImageTransparencyMode::Checker;
  QColor                m_imageViewerSolidColor       = QColor(Qt::white);
  QColor                m_imageViewerCheckerColor1    = QColor(0xC8, 0xC8, 0xC8);
  QColor                m_imageViewerCheckerColor2    = QColor(0xF0, 0xF0, 0xF0);

  // Binary viewer
  BinaryViewerUnit   m_binaryViewerUnit     = BinaryViewerUnit::Byte1;
  BinaryViewerEndian m_binaryViewerEndian   = BinaryViewerEndian::Little;
  QString            m_binaryViewerEncoding = QStringLiteral("UTF-8");
  QFont              m_binaryViewerFont;  // 既定は monospace (コンストラクタで初期化)
  QColor             m_binaryViewerNormalFg     = QColor(Qt::black);
  QColor             m_binaryViewerNormalBg;                 // 不正値ならパレット既定
  QColor             m_binaryViewerSelectedFg   = QColor(Qt::white);
  QColor             m_binaryViewerSelectedBg   = QColor(0x31, 0x6A, 0xC5);
  QColor             m_binaryViewerAddressFg    = QColor(Qt::darkGray);
  QColor             m_binaryViewerAddressBg    = QColor(0xF0, 0xF0, 0xF0);
  bool             m_persistHistory  = false;
  QStringList      m_paneHistory[static_cast<int>(PaneType::Count)];
  QString          m_autoRenameTemplate = QStringLiteral(" ({n})");
  bool             m_defaultDeleteToTrash = true;
  bool             m_progressAutoClose    = false;
  QStringList      m_searchExcludeDirs    = { QStringLiteral(".*") };
  QList<Bookmark>  m_bookmarks;
  // 初回起動時のデフォルトブックマーク注入を一度きりに制限するためのフラグ。
  // false のままの設定ファイルを読むと、既存ブックマークにマージしてから true にする。
  bool             m_defaultBookmarksInstalled = false;

  // 外部アプリ (UserCommand) 一覧。デフォルトは applyDefaults() で
  // defaultBuiltinUserCommands() (terminal + editor) を投入する。
  QList<UserCommand> m_userCommands;

  // Window settings
  WindowSizeMode     m_windowSizeMode     = WindowSizeMode::Default;
  QSize              m_customWindowSize   = QSize(1200, 600);
  QSize              m_lastWindowSize     = QSize(1200, 600);
  WindowPositionMode m_windowPositionMode = WindowPositionMode::Default;
  QPoint             m_customWindowPosition;
  QPoint             m_lastWindowPosition;

  // ── テーマスキーム ────────────────────────
  // m_ 個別フィールド (m_font / m_addressForeground / ...) が「現在の
  // ワーキング値」、以下 2 つが Light/Dark のスナップショット。
  // save() の直前に collectThemeFields() でアクティブ側を更新し、
  // load() / setThemeMode() の直後に applyThemeFields() で m_ 側へ書き戻す。
  ColorScheme        m_lightScheme;
  ColorScheme        m_darkScheme;
  ThemeMode          m_themeMode = ThemeMode::Auto;

  // 現在 effectiveTheme() の値をキャッシュする (OS 追従のシグナル受信時に
  // 直前と同じならシグナル抑止できるよう)。
  mutable ThemeMode  m_lastEffective = ThemeMode::Light;

  // OS が現在 Light か Dark か。Settings 構築時に
  // `QStyleHints::colorScheme()` を一度読んで初期値とし、それ以降は
  // colorSchemeChanged シグナル経由で更新する。`applyThemeFields()` 内で
  // 私たちが `setColorScheme()` を呼ぶと colorScheme() の戻り値が
  // 上書きされてしまうため、OS 状態を独立に追跡しないと
  // detectOsTheme() が正しい結果を返せなくなる。
  mutable ThemeMode  m_osColorScheme = ThemeMode::Light;

  // m_ フィールド ↔ ColorScheme のシリアライズ。private 実装ヘルパ。
  ColorScheme collectThemeFields() const;
  void        applyThemeFields(const ColorScheme& s);

};

} // namespace Farman
