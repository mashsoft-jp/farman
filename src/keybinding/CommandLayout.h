#pragma once

#include "ICommand.h"
#include <QList>
#include <QString>

namespace Farman {

class CommandRegistry;

// カテゴリ別の表示用情報。`commandsGroupedByCategory()` が返す。
struct CommandCategoryGroup {
  QString          id;        // カテゴリ ID ("navigation" 等)
  QString          display;   // 表示名 (`tr()` 適用済み)
  QList<ICommand*> commands;  // そのカテゴリに属するコマンド (登録順)
};

// CommandRegistry::allCommands() を「事前定義したグループ順」で
// グループ化して返す。キーバインド一覧 / Keybindings タブ等、複数の
// UI で表示順を統一するための共通ヘルパ。
//
// - グループ構成・並びは Web マニュアルの「キーバインド一覧」と揃える:
//   ナビゲーション・選択 → ファイル操作 → ビュアー・表示・ペイン →
//   ブックマーク・履歴・アプリ → その他 (Other)。
// - 各グループ先頭はマニュアルの行順 (CommandLayout.cpp の headOrder)、
//   それ以外は CommandRegistry の登録順を保持する。
// - 既知でないカテゴリのコマンドは "general" (= Other) に集約される。
// - 空のグループは結果に含めない。
QList<CommandCategoryGroup> commandsGroupedByCategory(
  const CommandRegistry& registry);

} // namespace Farman
