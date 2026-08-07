#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QKeySequence>

namespace Farman {

// 同梱ビュアーの「再割り当て可能な」ショートカットコマンド 1 件の定義。
// commandId は "viewer.<viewer>.<action>" 形式で全体一意。defaultKeys は
// 1 コマンドに複数キー（例: バイナリの次を検索 = Cmd/Ctrl+G と F3）を許す。
struct ViewerCommandDef {
  QString viewerId;                 // "text" / "csv" / "markdown" / "pdf" / "image" / "binary" / "media"
  QString commandId;                // "viewer.text.toggle_word_wrap" 等
  QString label;                    // 設定 UI 表示名 (tr 済み)
  QList<QKeySequence> defaultKeys;  // 既定キー（0 個も可 = 既定は未割当）
};

// 全ビュアーの全コマンド定義（表示順）。ここが「どのキーが可変か」の唯一の一覧。
// ナビゲーション（カーソル移動・スクロール）や、閉じる/検索欄の Enter・Esc は
// 固定扱いなので含めない。
QList<ViewerCommandDef> viewerCommandDefs();

// 定義に現れるビュアー ID を表示順で返す（設定 UI のセクション分け用）。
QStringList viewerCommandViewerIds();

// ビュアー ID → ローカライズ表示名（"テキストビュアー" 等）。設定 UI のセクション
// 見出しに使う。ViewerNames コンテキストの訳を共有する。
QString viewerCommandViewerName(const QString& viewerId);

} // namespace Farman
