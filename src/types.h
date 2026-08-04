#pragma once

#include <QtGlobal>
#include <QString>
#include <QColor>

namespace Farman {

// ペインの種別
enum class PaneType {
  Left = 0,
  Right,
  Count
};

// ファイルマネージャパネルのレイアウト。
//   - Dual:    左右 2 ペイン (従来のデフォルト)。
//   - Single:  シングルペイン (反対側ペインを非表示)。
//   - Preview: 左ペイン (ファイル一覧) + 右ペイン (ビュアー)。
//              カーソル移動でビュアー内容が逐次切り替わる Quick View モード。
//              Sync Browse / Directory Compare は強制 OFF、Tab キーは no-op。
//              Phase 1 ではフォーカスは常に左ペイン側に固定 (プレビューでの
//              文字選択 / コピーは Enter で本来のビュアーを開いてから行う)。
enum class LayoutMode {
  Dual = 0,
  Single,
  Preview
};

inline const char* layoutModeKey(LayoutMode m) {
  switch (m) {
    case LayoutMode::Single:  return "single";
    case LayoutMode::Preview: return "preview";
    case LayoutMode::Dual:
    default:                  return "dual";
  }
}

inline LayoutMode layoutModeFromKey(const QString& key, LayoutMode fallback = LayoutMode::Dual) {
  if (key == QLatin1String("single"))  return LayoutMode::Single;
  if (key == QLatin1String("preview")) return LayoutMode::Preview;
  if (key == QLatin1String("dual"))    return LayoutMode::Dual;
  return fallback;
}

// ソートキー
enum class SortKey {
  None,          // ソートなし（第2キー用）
  Name,
  Size,
  Type,
  LastModified
};

// ディレクトリのソート位置
enum class SortDirsType {
  First,   // ディレクトリを先頭に
  Last,    // ディレクトリを末尾に
  Mixed    // ファイルと混在
};

// ファイルサイズの表示形式
enum class FileSizeFormat {
  Bytes,   // バイト表示
  SI,      // KB / MB / GB (1000進)
  IEC,     // KiB / MiB / GiB (1024進)
  Auto     // 自動選択
};

// ディレクトリサイズのバックグラウンド算出のキャッシュ方針。
enum class DirSizeCacheMode {
  Always,  // 一覧表示のたびに毎回再計算する
  Cached   // 一度算出したら一定時間 (directorySizeCacheSeconds) キャッシュを使う
};

// 属性フィルタのフラグ
enum class AttrFilter : quint32 {
  None        = 0x00,
  ShowHidden  = 0x01,  // 隠しファイルを表示
  ShowSystem  = 0x02,  // システムファイルを表示 (Windows)
  DirsOnly    = 0x04,  // ディレクトリのみ
  FilesOnly   = 0x08,  // ファイルのみ
};
Q_DECLARE_FLAGS(AttrFilterFlags, AttrFilter)
Q_DECLARE_OPERATORS_FOR_FLAGS(AttrFilterFlags)

// ワーカーの操作種別
enum class WorkerOperation {
  Copy,
  Move,
  Remove
};

// 上書き時の動作モード（操作開始前に選択）
enum class OverwriteMode {
  Ask,            // 都度確認
  AutoOverwrite,  // 自動で上書き
  AutoRename      // 自動でリネーム（"foo (1).txt" など）
};

// 上書き競合発生時のユーザー選択（Ask モードの OverwriteDialog からの応答）
struct OverwriteDecision {
  enum class Action {
    Overwrite,  // そのまま上書き
    Rename,     // 任意のファイル名でリネーム
    Cancel      // 操作全体をキャンセル
  };
  Action  action  = Action::Cancel;
  QString newName;  // Rename 選択時のみ有効（ファイル名のみ、パスを含まない）
};

// ウィンドウサイズの復元モード
enum class WindowSizeMode {
  Default,      // デフォルトサイズ (1200x600)
  LastSession,  // 前回終了時のサイズ
  Custom        // ユーザー指定のサイズ
};

// ウィンドウ位置の復元モード
enum class WindowPositionMode {
  Default,      // デフォルト位置 (画面中央)
  LastSession,  // 前回終了時の位置
  Custom        // ユーザー指定の位置
};

// アプリの UI 言語
enum class LanguageMode {
  Auto,      // システムロケールに従う
  English,   // 強制 en
  Japanese   // 強制 ja
};

// 配色テーマモード。
// - Auto:  OS の Light / Dark 設定に追従。Qt 6.5+ の
//          `QGuiApplication::styleHints()->colorScheme()` を読む。
// - Light: 強制的に Light スキームを使用。
// - Dark:  強制的に Dark スキームを使用。
enum class ThemeMode {
  Auto,
  Light,
  Dark
};

// 初期表示ディレクトリのモード（ペイン別）
enum class InitialPathMode {
  Default,      // ホームディレクトリ
  LastSession,  // 前回終了時のパス
  Custom        // ユーザー指定の固定パス
};

// ファイルリストのカーソル形状
enum class CursorShape {
  Underline,    // 行の下端に横棒を描画 (太さは Settings::cursorThickness で指定)
  RowBackground // 行全体の背景色を変える
};

// パネルの表示モード。「リスト」+「サムネイル 3 サイズ」の合計 4 値で、
// FileListPane のフッタ popup から 1 つを選択する。ショートカット
// (view.toggle_thumbnails) で List → Small → Medium → Large → List の順に
// 巡回する。
//
// 内部実装上は QStackedWidget で List ビュー (QTableView) と Thumbnail ビュー
// (QListView::IconMode) を切替える。Thumbnail* のときだけ実画像のサムネイル
// 描画が走り、Small/Medium/Large の値で外接サイズ (px) が決まる。
enum class ListViewMode {
  List            = 0,
  ThumbnailSmall  = 1,
  ThumbnailMedium = 2,
  ThumbnailLarge  = 3
};

// サムネイル描画時の外接サイズ (px)。ListViewMode から導出するヘルパとして
// 使う (Settings には直接保存しない、ListViewMode が真の保持先)。
enum class ThumbnailSize {
  Small  = 96,
  Medium = 160,
  Large  = 256
};

// ListViewMode が Thumbnail*いずれかを表すか。
inline bool isThumbnailMode(ListViewMode m) {
  return m != ListViewMode::List;
}

// ListViewMode から描画用の外接 px を取り出す。List のときは Medium 相当を返す
// (ビューは hidden だが内部状態として持っておく)。
inline int thumbnailPixelSizeFor(ListViewMode m) {
  switch (m) {
    case ListViewMode::ThumbnailSmall:  return static_cast<int>(ThumbnailSize::Small);
    case ListViewMode::ThumbnailLarge:  return static_cast<int>(ThumbnailSize::Large);
    case ListViewMode::ThumbnailMedium:
    case ListViewMode::List:
    default:                            return static_cast<int>(ThumbnailSize::Medium);
  }
}

// 4 値を List → S → M → L → List の順に巡回する。Cmd+G が 1 回押されるごとに
// 1 段階進む。
inline ListViewMode nextListViewMode(ListViewMode m) {
  switch (m) {
    case ListViewMode::List:            return ListViewMode::ThumbnailSmall;
    case ListViewMode::ThumbnailSmall:  return ListViewMode::ThumbnailMedium;
    case ListViewMode::ThumbnailMedium: return ListViewMode::ThumbnailLarge;
    case ListViewMode::ThumbnailLarge:
    default:                            return ListViewMode::List;
  }
}

// ビュアーの表示方法。
// - Inline:   メインウィンドウ内の ViewerPanel で表示 (現状の動作)。Enter / Esc
//             でファイルマネージャパネルに戻る。同時に 1 ファイルしか開けないが
//             キーボードで完結する。
// - External: ファイルごとに独立した *ViewerWindow を生成して表示する。複数
//             ファイルを並べたり別ディスプレイへ飛ばせる。
enum class ViewerMode {
  Inline,
  External
};

// 画像ビュアーの透明部分の表示方法
enum class ImageTransparencyMode {
  SolidColor, // 指定した背景色で塗りつぶす
  Checker     // 市松模様 (チェック柄) で表示する
};

// バイナリビュアーの 16 進ダンプ単位
enum class BinaryViewerUnit {
  Byte1,
  Byte2,
  Byte4,
  Byte8
};

// バイナリビュアーの多バイト単位表示時のエンディアン
enum class BinaryViewerEndian {
  Little,
  Big
};

// PDF ビュアーの既定表示の合わせ方。
enum class PdfViewerFitMode {
  ActualSize,  // 等倍 (ズーム指定)
  FitWidth,    // ウィンドウ幅に合わせる
  FitPage      // ページ全体を表示
};

// ファイル一覧でのカラーリング対象カテゴリ
enum class FileCategory {
  Normal    = 0,  // 通常ファイル
  Hidden    = 1,  // 隠しファイル／ディレクトリ（優先度: 最高）
  Directory = 2,  // 非隠しのディレクトリ
  Count     = 3
};

// ファイルカテゴリごとの表示スタイル
struct CategoryColor {
  QColor foreground;   // 文字色（invalid なら既定を使用）
  QColor background;   // 背景色（invalid なら背景を上書きしない）
  bool   bold = false; // 太字で表示するか
};

// ファイル種別ごとのカラーリング設定 (glob パターン → 前景/背景色 + 太字)。
// 本来 Settings.h に置きたいが、テーマ用の ColorScheme から参照する都合で
// ここに先出しする (Settings.h は ColorScheme.h を include するため)。
struct ColorRule {
  QString pattern;     // glob パターン: "*.cpp", "*.h"
  QColor  foreground;
  QColor  background;
  bool    bold = false;
};

} // namespace Farman
