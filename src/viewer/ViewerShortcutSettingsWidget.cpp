#include "ViewerShortcutSettingsWidget.h"

#include "keybinding/ViewerCommands.h"
#include "keybinding/ViewerKeyBindingManager.h"

#include <QGridLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QKeySequenceEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QHash>

namespace Farman {

ViewerShortcutSettingsWidget::ViewerShortcutSettingsWidget(const QString& viewerId,
                                                           QWidget* parent)
  : QWidget(parent), m_viewerId(viewerId) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);

  auto* hint = new QLabel(
    tr("Click a key field and press the new key. Clear a field to unbind."), this);
  hint->setWordWrap(true);
  root->addWidget(hint);

  auto* grid = new QGridLayout();
  grid->setColumnStretch(0, 1);
  root->addLayout(grid);

  auto& vkb = ViewerKeyBindingManager::instance();
  int r = 0;
  for (const ViewerCommandDef& def : viewerCommandDefs()) {
    if (def.viewerId != m_viewerId) {
      continue;
    }
    Row row;
    row.commandId   = def.commandId;
    row.defaultKeys = def.defaultKeys;
    const QList<QKeySequence> cur = vkb.keysFor(def.commandId);
    row.initialKey = cur.isEmpty() ? QKeySequence() : cur.first();

    auto* label = new QLabel(def.label, this);
    auto* edit  = new QKeySequenceEdit(this);
    edit->setMaximumSequenceLength(1);  // ビュアーキーは 1 打鍵に限定
    edit->setKeySequence(row.initialKey);
    row.edit = edit;

    // クリア（未割当）ボタン
    auto* clearBtn = new QPushButton(tr("Clear"), this);
    QObject::connect(clearBtn, &QPushButton::clicked, edit,
                     [edit]() { edit->clear(); });

    grid->addWidget(label,    r, 0);
    grid->addWidget(edit,     r, 1);
    grid->addWidget(clearBtn, r, 2);
    ++r;

    m_rows.append(row);
  }
  root->addStretch(1);
}

void ViewerShortcutSettingsWidget::save() {
  auto& vkb = ViewerKeyBindingManager::instance();
  bool changed = false;

  // 変更されたコマンドだけ setKeys する（未変更は既定の複数キー等を保つ）。
  for (const Row& row : m_rows) {
    const QKeySequence seq = row.edit->keySequence();
    if (seq == row.initialKey) {
      continue;
    }
    changed = true;
    if (seq.isEmpty()) {
      vkb.setKeys(row.commandId, {});
    } else {
      vkb.setKeys(row.commandId, {seq});
    }
  }

  if (!changed) {
    return;
  }

  // 同一ビュアー内でキーが重複していないか確認（重複時は先勝ちになる旨を警告）。
  QHash<QString, QString> used;  // key text -> commandId（最初に見つかったもの）
  QStringList conflicts;
  for (const Row& row : m_rows) {
    const QKeySequence seq = row.edit->keySequence();
    if (seq.isEmpty()) {
      continue;
    }
    const QString k = seq.toString();
    if (used.contains(k)) {
      conflicts << seq.toString(QKeySequence::NativeText);
    } else {
      used.insert(k, row.commandId);
    }
  }
  if (!conflicts.isEmpty()) {
    QMessageBox::warning(
      this, tr("Duplicate shortcut"),
      tr("Some keys are assigned to more than one command in this viewer "
         "(%1). Only the first one will take effect.")
        .arg(conflicts.join(QStringLiteral(", "))));
  }

  vkb.saveToSettings();
}

void ViewerShortcutSettingsWidget::restoreDefaults() {
  for (const Row& row : m_rows) {
    row.edit->setKeySequence(
      row.defaultKeys.isEmpty() ? QKeySequence() : row.defaultKeys.first());
  }
}

} // namespace Farman
