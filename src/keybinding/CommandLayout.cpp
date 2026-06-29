#include "CommandLayout.h"
#include "CommandRegistry.h"
#include <QCoreApplication>
#include <QMap>

namespace Farman {

namespace {

// グループ構成と並びは Web マニュアル (docs/manual, docs/en/manual) の
// 「キーバインド一覧」セクションに合わせる。マニュアル側を変えるときは
// ここも揃えること。
//   - categories: このグループに属する CommandRegistry のカテゴリ ID
//   - headOrder : マニュアルの行順に合わせて先頭に並べるコマンド ID。
//                 ここに無いコマンドは登録順でグループ末尾に続ける。
//                 headOrder はカテゴリよりも優先される (例: file.search は
//                 file カテゴリだがマニュアルに合わせてビュアー・表示・
//                 ペインに置く)。
struct GroupSpec {
  const char* id;
  QString     display;
  QStringList categories;
  QStringList headOrder;
};

QList<GroupSpec> groupTemplate() {
  return {
    { "navsel",
      QCoreApplication::translate("CommandLayout", "Navigation & selection"),
      { QStringLiteral("navigation"), QStringLiteral("selection") },
      { QStringLiteral("navigate.up"), QStringLiteral("navigate.down"),
        QStringLiteral("navigate.left"), QStringLiteral("navigate.right"),
        QStringLiteral("navigate.home"), QStringLiteral("navigate.end"),
        QStringLiteral("navigate.pageup"), QStringLiteral("navigate.pagedown"),
        QStringLiteral("navigate.enter"), QStringLiteral("navigate.parent"),
        QStringLiteral("select.toggle_and_down"), QStringLiteral("select.toggle"),
        QStringLiteral("select.invert"), QStringLiteral("select.all") } },
    { "fileops",
      QCoreApplication::translate("CommandLayout", "File operations"),
      { QStringLiteral("file") },
      { QStringLiteral("file.copy"), QStringLiteral("file.move"),
        QStringLiteral("file.mkdir"), QStringLiteral("file.newfile"),
        QStringLiteral("file.delete"), QStringLiteral("file.rename"),
        QStringLiteral("file.bulk_rename"), QStringLiteral("file.copy_path"),
        QStringLiteral("file.attributes"), QStringLiteral("file.pack"),
        QStringLiteral("file.unpack"), QStringLiteral("file.execute") } },
    { "viewpane",
      QCoreApplication::translate("CommandLayout", "Viewers, view & panes"),
      { QStringLiteral("view"), QStringLiteral("pane") },
      { QStringLiteral("view.file"), QStringLiteral("view.choose"),
        QStringLiteral("pane.sort_filter"), QStringLiteral("view.quick_filter"),
        QStringLiteral("pane.toggle_single"), QStringLiteral("view.toggle_log"),
        QStringLiteral("pane.sync_browse_toggle"),
        QStringLiteral("pane.sync_other_to_active"),
        QStringLiteral("pane.sync_active_to_other"),
        QStringLiteral("file.search") } },
    { "bookhistapp",
      QCoreApplication::translate("CommandLayout", "Bookmarks, history & app"),
      { QStringLiteral("bookmark"), QStringLiteral("history"),
        QStringLiteral("app"), QStringLiteral("tools"), QStringLiteral("help") },
      { QStringLiteral("bookmark.toggle"), QStringLiteral("bookmark.list"),
        QStringLiteral("history.show"), QStringLiteral("user.cmd.terminal"),
        QStringLiteral("user.cmd.editor"), QStringLiteral("app.settings"),
        QStringLiteral("help.shortcuts"), QStringLiteral("app.quit") } },
    { "general",
      QCoreApplication::translate("CommandLayout", "Other"),
      {},  // どのグループにも属さないカテゴリの受け皿
      {} },
  };
}

} // namespace

QList<CommandCategoryGroup> commandsGroupedByCategory(
  const CommandRegistry& registry)
{
  const QList<GroupSpec> specs = groupTemplate();

  // headOrder のコマンド ID → グループ index (カテゴリより優先)
  QMap<QString, int> claimedIdx;
  // カテゴリ ID → グループ index
  QMap<QString, int> categoryIdx;
  for (int i = 0; i < specs.size(); ++i) {
    for (const QString& id : specs[i].headOrder) claimedIdx.insert(id, i);
    for (const QString& cat : specs[i].categories) categoryIdx.insert(cat, i);
  }
  const int fallback = int(specs.size()) - 1;  // "general"

  // 登録順を保ったままグループへ振り分ける
  QList<QList<ICommand*>> buckets(specs.size());
  for (ICommand* cmd : registry.allCommands()) {
    if (!cmd) continue;
    int i = claimedIdx.value(cmd->id(), -1);
    if (i < 0) i = categoryIdx.value(cmd->category(), fallback);
    buckets[i].append(cmd);
  }

  // 各グループ内: headOrder の順 → 残りは登録順
  QList<CommandCategoryGroup> result;
  for (int i = 0; i < specs.size(); ++i) {
    QList<ICommand*> ordered;
    for (const QString& id : specs[i].headOrder) {
      for (ICommand* cmd : buckets[i]) {
        if (cmd->id() == id) { ordered.append(cmd); break; }
      }
    }
    for (ICommand* cmd : buckets[i]) {
      if (!ordered.contains(cmd)) ordered.append(cmd);
    }
    if (ordered.isEmpty()) continue;
    result.append({ QString::fromLatin1(specs[i].id), specs[i].display,
                    std::move(ordered) });
  }
  return result;
}

} // namespace Farman
