#pragma once

#include <QColor>
#include <QFont>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <QtPlugin>

#include "keybinding/ViewerCommands.h"  // ViewerCommandDef（shortcutCommands の戻り値）

namespace Farman {

// 外部ビュアープラグインに渡す Appearance のスナップショット。
// Settings や QApplication への参照は渡さず、プラグイン API に必要な値だけ
// 固定形で渡す。
struct PluginAppearance {
  enum class Theme { Light, Dark };

  Theme  theme = Theme::Light;
  QFont  uiFont;
  QColor windowBackground;
  QColor panelBackground;
  QColor text;
  QColor mutedText;
  QColor accent;
  QColor selectionBackground;
  QColor selectionText;
};

// プラグインがアプリ本体側の情報を必要とした時の窓口。
//
// フィールド追加は構造体ベースで行う。「createViewer の引数を増やす」のような
// 署名変更を避けるため、追加情報はすべてここに足す形にする。
//
// 値渡しせず const 参照で渡すこと。プラグイン側はメンバの読み取りのみ。
struct PluginContext {
  // 呼び出し時の farman アプリのバージョン文字列 (例 "0.9.0")。
  // プラグインが「自分は farman 1.x 以降が必要」のような互換性判定をするために。
  QString farmanVersion;

  // 起動時 / createViewer 呼び出し時点の Appearance スナップショット。
  // 起動後の変更は IViewerPlugin::appearanceChanged() で通知される。
  PluginAppearance appearance;

  // 将来追加候補:
  //   - QObject* logger;        // Logger インスタンスへの参照
  //   - QSettings* settings;    // プラグイン自身の設定保存先
  //   - QString configDir;      // プラグイン固有のキャッシュ / 設定ディレクトリ
  //   - QWidget* mainWindow;    // モーダルダイアログの親に使う等
  // 構造体メンバの追加は ABI 互換ではないが、再コンパイルで対応可。
  // プラグイン作者には「PluginContext のサイズは固定でない」ことを明示する。
};

// Settings を再ロードし、プラグイン側 Logger をアプリ本体と同じ設定に揃える。
// プラグイン dylib は Settings / Logger シングルトンを自前で持つ (本体とは別
// インスタンス) ため、これが無いと createViewer 後の非同期ロード結末
// (logViewerLoadResult) がどこにも記録されない。
// 各プラグインは起動時 (initialize) と設定変更時 (appearanceChanged が
// settingsChanged の通知経路を兼ねる) の両方から呼んで、ログ出力 ON/OFF /
// ディレクトリ / 保持日数の変更にも追従させること。
void syncPluginFromHostSettings();

// 必要な farman 本体の最小バージョン (MinHostVersion) について
// ------------------------------------------------------------------
// プラグインが本体側の機能・同梱ライブラリ (例: libQt6OpenGL) を前提とする場合、
// Q_PLUGIN_METADATA の FILE json に必要な本体最小バージョンを宣言できる:
//
//   Q_PLUGIN_METADATA(IID FarmanIViewerPlugin_iid FILE "metadata.json")
//   // metadata.json: { "MinHostVersion": "0.9.9" }
//
// ViewerDispatcher / ArchiveDispatcher は各プラグインをロードする「前」に
// QPluginLoader::metaData() で MetaData.MinHostVersion を読み、本体バージョン
// (数値部のみで比較、prerelease は無視。utils/PluginCompat.h) がそれ未満なら
// instance() を呼ばずにスキップし、Settings > Plugins に「要 farman X 以降」と
// 表示する。依存不足で dlopen が失敗するケースでも、メタデータはコード解決前に
// 読めるため原因の分かる形で弾ける。宣言が無ければ従来どおりロードを試みる。

// ビュアープラグインのインターフェース。
// IID は `com.farman.IViewerPlugin/<major>.<minor>` 形式。互換性のない変更時に
// メジャー番号を上げ、Dispatcher で両方のバージョンを試すことで段階移行する。
// プラグイン側の Q_PLUGIN_METADATA でも必ずこのマクロを使うこと
// (文字列を直書きすると IID 更新時に追従漏れする)。
#define FarmanIViewerPlugin_iid "com.farman.IViewerPlugin/5.0"
class IPluginSettingsPage;  // IPluginSettingsPage.h
class IViewerPlugin {
public:
  virtual ~IViewerPlugin() = default;

  // ── プラグイン識別情報 ─────────────────────────
  virtual QString     pluginId()   const = 0;  // "text_viewer"
  virtual QString     pluginName() const = 0;  // "テキストビュアー"
  // 制作者情報。外部プラグインは author() (制作者名 / 組織名) を必ず返すこと
  // (空だとロード時にエラーとなり使用不可)。authorUrl() は任意で、Web サイト
  // や連絡先の URL を返すと詳細ダイアログにリンクとして表示される。
  virtual QString     author()    const { return {}; }  // "Mashsoft Inc."
  virtual QString     authorUrl() const { return {}; }  // "https://example.com"
  // プラグイン自身の配布バージョン (例 "0.9.8")。既定実装はビルド時の
  // FARMAN_VERSION を返す (同梱公式プラグインは farman 本体と同じ版数になる)。
  // 外部プラグインは自前の版数を返すよう override してよい。Settings → Plugins
  // で表示する。
  virtual QString     version()   const;
  // 優先度: 0 が最優先で、数値が小さいほど優先される。
  // ユーザー作成の外部プラグインは 0〜9999 を指定する (範囲外はロード時に
  // エラーとなり使用不可)。10000 以上は同梱公式プラグイン用の予約域:
  // PDF / CSV / Markdown = 10000、コアビュアーはメディア 99996 /
  // 画像 99997 / テキスト 99998 / バイナリ 99999 (最後のフォールバック)。
  virtual int         priority()   const { return 0; }

  // ── 対応ファイルの宣言 ─────────────────────────
  virtual QStringList supportedExtensions() const = 0;  // {"txt","log","cpp"}
  virtual QStringList supportedMimeTypes()  const = 0;  // {"text/plain"}

  // このファイルを開けるか (より細かい判定が必要な場合に override)
  virtual bool canHandle(const QString& filePath) const;

  // ── ライフサイクル (任意 override) ──────────────
  // プラグインロード直後に 1 回だけ呼ばれる。
  // 失敗時は false を返す (Dispatcher は登録を取り消す)。default 実装は何もしない。
  virtual bool initialize(const PluginContext& /*ctx*/) { return true; }
  // initialize() が成功したプラグインに対し、アンロード前に 1 回だけ呼ばれる
  // (initialize 失敗時や登録前に弾かれた場合は呼ばれない)。
  virtual void shutdown() {}

  // Appearance が変更されたときに呼ばれる。Settings ダイアログでの変更だけでなく、
  // OS のライト / ダーク自動切替に Auto モードで追随した場合も通知される。
  virtual void appearanceChanged(const PluginAppearance& /*appearance*/) {}

  // ── 機能宣言 (任意 override) ────────────────────
  // 将来のフィーチャー判定用。例: {"thumbnail", "async_load", "search"}.
  // 純粋仮想を追加せずに新機能の有無を見分けられるようにする。
  virtual QStringList capabilities() const { return {}; }

  // ── 設定 UI (任意 override) ─────────────────────
  // 設定画面を持つなら hasSettings() を true にし、createSettingsPage() で
  // IPluginSettingsPage 派生のページを返す。farman が Settings → Plugins →
  // 詳細の「設定...」から標準ダイアログ枠 (OK / Cancel / Apply / 既定に戻す)
  // に載せ、確定時に page->save() を呼ぶ。farman は save() 後に Settings を
  // 再読込して開いているビュアーへ反映する。ページの ownership は farman 側。
  virtual bool hasSettings() const { return false; }
  virtual IPluginSettingsPage* createSettingsPage(QWidget* /*parent*/) {
    return nullptr;
  }

  // このビュアーが持つ「設定可能なショートカット」の一覧を返す取得 API。
  // 本体 (Settings → キーバインド) がこれを呼んで一覧表示・編集する。既定は空。
  // 割り当ての保存は本体が一括で行い、変更結果は createViewer で返したビューの
  // Q_INVOKABLE applyShortcutBindings(QVariantMap) へ本体から push される
  // (データフローは本体→ビュアーの一方通行)。
  virtual QList<ViewerCommandDef> shortcutCommands() const { return {}; }

  // 拡張子の紐付けを自前の設定ページ内で編集する場合は true を返す。
  // farman は Settings → Plugins → 詳細でホスト側の「拡張子」欄を出さず、
  // 編集はプラグインの設定ページに一任する (ホストのグローバル関連付け
  // viewerAssociations もこのプラグインについては書き換えない)。
  // false (既定) のプラグインは従来どおりホスト側の欄で編集する。
  virtual bool managesOwnExtensions() const { return false; }

  // ── ビュアー生成 ──────────────────────────────
  // 呼び出し元がウィジェットの ownership を持つ。
  // 返す QWidget は Inline では埋め込み、External ではトップレベル化して表示する。
  // プラグイン側では show() せず、表示方法は呼び出し側に任せること。
  // ctx には farman 本体側の参照情報が入る (将来拡張)。
  virtual QWidget* createViewer(const QString&       filePath,
                                QWidget*             parent,
                                const PluginContext& ctx) = 0;
};

} // namespace Farman

// 5.0: shortcutCommands() の追加 (仮想関数追加による ABI 変更)。旧 IID の外部
//      プラグインは qobject_cast に失敗し、ロードエラーとして安全に拒否される。
// 4.0: hasSettings() / createSettingsPage() / managesOwnExtensions() の追加。
// 3.0: author() / authorUrl() の追加。
// 仮想関数の追加・削除・並び替えなど ABI 互換が壊れる変更をしたら、
// 必ずこの IID のバージョンを上げること (旧 IID のプラグインは
// qobject_cast に失敗し、ロードエラーとして安全に拒否される)。
// 2.0: appearanceChanged() の追加と priority() の意味変更 (0 が最優先)。
Q_DECLARE_INTERFACE(Farman::IViewerPlugin, FarmanIViewerPlugin_iid)
