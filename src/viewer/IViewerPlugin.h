#pragma once

#include <QColor>
#include <QFont>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <QtPlugin>

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

// ビュアープラグインのインターフェース。
// IID は `com.farman.IViewerPlugin/<major>.<minor>` 形式。互換性のない変更時に
// メジャー番号を上げ、Dispatcher で両方のバージョンを試すことで段階移行する。
class IViewerPlugin {
public:
  virtual ~IViewerPlugin() = default;

  // ── プラグイン識別情報 ─────────────────────────
  virtual QString     pluginId()   const = 0;  // "text_viewer"
  virtual QString     pluginName() const = 0;  // "テキストビュアー"
  virtual int         priority()   const { return 0; }  // 高いほど優先

  // ── 対応ファイルの宣言 ─────────────────────────
  virtual QStringList supportedExtensions() const = 0;  // {"txt","log","cpp"}
  virtual QStringList supportedMimeTypes()  const = 0;  // {"text/plain"}

  // このファイルを開けるか (より細かい判定が必要な場合に override)
  virtual bool canHandle(const QString& filePath) const;

  // ── ライフサイクル (任意 override) ──────────────
  // プラグインロード直後に 1 回だけ呼ばれる。
  // 失敗時は false を返す (Dispatcher は登録を取り消す)。default 実装は何もしない。
  virtual bool initialize(const PluginContext& /*ctx*/) { return true; }
  // プラグインアンロード前に 1 回だけ呼ばれる。
  virtual void shutdown() {}

  // Appearance が変更されたときに呼ばれる。Settings ダイアログでの変更だけでなく、
  // OS のライト / ダーク自動切替に Auto モードで追随した場合も通知される。
  virtual void appearanceChanged(const PluginAppearance& /*appearance*/) {}

  // ── 機能宣言 (任意 override) ────────────────────
  // 将来のフィーチャー判定用。例: {"thumbnail", "async_load", "search"}.
  // 純粋仮想を追加せずに新機能の有無を見分けられるようにする。
  virtual QStringList capabilities() const { return {}; }

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

Q_DECLARE_INTERFACE(Farman::IViewerPlugin, "com.farman.IViewerPlugin/1.0")
