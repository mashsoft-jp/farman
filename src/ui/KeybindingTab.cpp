#include "KeybindingTab.h"
#include "keybinding/KeyBindingManager.h"
#include "keybinding/CommandLayout.h"
#include "keybinding/CommandRegistry.h"
#include "keybinding/ICommand.h"
#include "keybinding/ViewerCommands.h"
#include "keybinding/ViewerKeyBindingManager.h"
#include "viewer/ViewerDispatcher.h"
#include "viewer/IViewerPlugin.h"
#include "settings/PresetIO.h"
#include "utils/Dialogs.h"
#include "utils/EnterClickFilter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QPushButton>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QFrame>
#include <QShortcut>
#include <QComboBox>
#include <QFileDialog>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QSet>

namespace Farman {

namespace {
// ビュアー節の見出し名を解決する。同梱ビュアーはカタログ (viewerCommandViewerName)
// から。未知 (外部プラグイン) の viewerId は、その viewerId を公開しているプラグインの
// 表示名 (pluginName) から解決する。見つからなければ viewerId をそのまま返す。
QString viewerSectionName(const QString& viewerId) {
  const QString bundled = viewerCommandViewerName(viewerId);
  if (bundled != viewerId) {
    return bundled;  // 同梱ビュアーの既知名
  }
  for (IViewerPlugin* p : ViewerDispatcher::instance().allPlugins()) {
    if (!p) {
      continue;
    }
    const QList<ViewerCommandDef> defs = p->shortcutCommands();
    if (!defs.isEmpty() && defs.first().viewerId == viewerId) {
      return p->pluginName();
    }
  }
  return viewerId;
}
} // namespace

KeybindingTab::KeybindingTab(QWidget* parent)
  : QWidget(parent)
  , m_table(nullptr)
  , m_resetButton(nullptr)
  , m_clearButton(nullptr)
  , m_recordFrame(nullptr)
  , m_recordCommandLabel(nullptr)
  , m_recordCurrentKeyLabel(nullptr)
  , m_recordNewKeyLabel(nullptr)
  , m_recordOkButton(nullptr)
  , m_recordCancelButton(nullptr)
  , m_editingRow(-1)
  , m_inRecordMode(false) {
  setupUi();
  loadKeybindings();
}

void KeybindingTab::setupUi() {
  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // ─── プリセット / インポート / エクスポート行 ───
  // 同梱プリセット (Default / Total Commander) をドロップダウンで選んだ
  // 瞬間に「New Key」列へプレビュー表示する。実際の全置換は SettingsDialog
  // の OK 時、Cancel すれば破棄される。1 行目は「(Choose a preset...)」の
  // プレースホルダで、ロード時の不意な発火を防ぐ役割を兼ねる。
  QHBoxLayout* presetRow = new QHBoxLayout();
  presetRow->addWidget(new QLabel(tr("Preset:"), this));
  m_presetCombo = new QComboBox(this);
  m_presetCombo->setMinimumWidth(220);
  m_presetCombo->setToolTip(tr("Selecting a preset previews it in the New Key column. "
                                "Press OK to commit or Cancel to revert."));
  m_presetCombo->addItem(tr("(Choose a preset...)"), QString());
  for (const auto& p : PresetIO::listBundledKeybindingPresets()) {
    m_presetCombo->addItem(p.displayName, p.resourcePath);
  }
  connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &KeybindingTab::onApplyPreset);
  presetRow->addWidget(m_presetCombo);

  presetRow->addSpacing(16);
  m_exportButton = new QPushButton(tr("Export..."), this);
  m_exportButton->setToolTip(tr("Save the current bindings to a JSON file"));
  connect(m_exportButton, &QPushButton::clicked, this, &KeybindingTab::onExport);
  presetRow->addWidget(m_exportButton);

  m_importButton = new QPushButton(tr("Import..."), this);
  m_importButton->setToolTip(tr("Load bindings from a JSON file (replaces existing)"));
  connect(m_importButton, &QPushButton::clicked, this, &KeybindingTab::onImport);
  presetRow->addWidget(m_importButton);

  presetRow->addStretch();
  mainLayout->addLayout(presetRow);

  // Info label
  QLabel* tableInfoLabel = new QLabel(tr("Press Enter or double-click to change a keybinding."), this);
  tableInfoLabel->setWordWrap(true);
  mainLayout->addWidget(tableInfoLabel);

  // Table
  m_table = new QTableWidget(this);
  m_table->setWordWrap(false);  // 折り返さず省略 (…) で 1 行表示
  m_table->setStyleSheet(inactiveSelectionStyleSheet());
  m_table->setColumnCount(3);
  m_table->setHorizontalHeaderLabels({tr("Command"), tr("Current Key"), tr("New Key")});
  m_table->horizontalHeader()->setStretchLastSection(false);
  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch); // Command column stretches
  m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);   // Current Key fixed width
  m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);   // New Key fixed width
  m_table->horizontalHeader()->resizeSection(1, 200); // Current Key width
  m_table->horizontalHeader()->resizeSection(2, 200); // New Key width (same as Current Key)
  m_table->verticalHeader()->setVisible(false);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

  // Install event filter on table for Enter key
  m_table->installEventFilter(this);

  connect(m_table, &QTableWidget::cellDoubleClicked, this, &KeybindingTab::onCellDoubleClicked);

  mainLayout->addWidget(m_table);

  // Key recording frame (initially hidden, shown below table when editing)
  m_recordFrame = new QFrame(this);
  m_recordFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
  m_recordFrame->setVisible(false);

  QVBoxLayout* recordLayout = new QVBoxLayout(m_recordFrame);
  recordLayout->setSpacing(16);
  recordLayout->setContentsMargins(16, 16, 16, 16);

  // Command name section
  QHBoxLayout* commandLayout = new QHBoxLayout();
  QLabel* commandTitleLabel = new QLabel(tr("Command:"), m_recordFrame);
  QFont titleFont = commandTitleLabel->font();
  titleFont.setBold(true);
  commandTitleLabel->setFont(titleFont);
  commandLayout->addWidget(commandTitleLabel);

  m_recordCommandLabel = new QLabel(m_recordFrame);
  m_recordCommandLabel->setWordWrap(true);
  commandLayout->addWidget(m_recordCommandLabel, 1);
  recordLayout->addLayout(commandLayout);

  // Key binding section (old -> new)
  QFont monoFont("Monospace");
  monoFont.setStyleHint(QFont::TypeWriter);

  QHBoxLayout* keyLayout = new QHBoxLayout();

  // Current key
  m_recordCurrentKeyLabel = new QLabel(m_recordFrame);
  m_recordCurrentKeyLabel->setStyleSheet("padding: 10px; background-color: palette(light); border-radius: 4px;");
  m_recordCurrentKeyLabel->setFont(monoFont);
  m_recordCurrentKeyLabel->setAlignment(Qt::AlignCenter);
  m_recordCurrentKeyLabel->setMinimumWidth(150);
  keyLayout->addWidget(m_recordCurrentKeyLabel);

  // Spacer before arrow
  keyLayout->addStretch(1);

  // Arrow
  QLabel* arrowLabel = new QLabel(tr("→"), m_recordFrame);
  QFont arrowFont = arrowLabel->font();
  arrowFont.setPointSize(arrowFont.pointSize() + 4);
  arrowLabel->setFont(arrowFont);
  keyLayout->addWidget(arrowLabel);

  // Spacer after arrow
  keyLayout->addStretch(1);

  // New key
  m_recordNewKeyLabel = new QLabel(tr("<press a key>"), m_recordFrame);
  m_recordNewKeyLabel->setStyleSheet("padding: 12px; background-color: palette(highlight); color: palette(highlighted-text); border-radius: 4px; font-weight: bold;");
  QFont newKeyFont = monoFont;
  newKeyFont.setPointSize(newKeyFont.pointSize() + 2);
  newKeyFont.setBold(true);
  m_recordNewKeyLabel->setFont(newKeyFont);
  m_recordNewKeyLabel->setAlignment(Qt::AlignCenter);
  m_recordNewKeyLabel->setMinimumWidth(150);
  keyLayout->addWidget(m_recordNewKeyLabel);

  recordLayout->addLayout(keyLayout);

  // Buttons
  QHBoxLayout* recordButtonLayout = new QHBoxLayout();

#ifdef Q_OS_MACOS
  m_recordCancelButton = new QPushButton(tr("Cancel (Cmd+C)"), m_recordFrame);
  m_recordOkButton = new QPushButton(tr("OK (Enter)"), m_recordFrame);
#else
  m_recordCancelButton = new QPushButton(tr("Cancel (Ctrl+C)"), m_recordFrame);
  m_recordOkButton = new QPushButton(tr("OK (Enter)"), m_recordFrame);
#endif

  connect(m_recordCancelButton, &QPushButton::clicked, this, &KeybindingTab::onRecordCancel);
  connect(m_recordOkButton, &QPushButton::clicked, this, &KeybindingTab::onRecordOk);

  recordButtonLayout->addStretch();
  recordButtonLayout->addWidget(m_recordCancelButton);
  recordButtonLayout->addWidget(m_recordOkButton);

  recordLayout->addLayout(recordButtonLayout);

  // Install event filter on record frame to capture keys
  m_recordFrame->installEventFilter(this);

  mainLayout->addWidget(m_recordFrame);

  // Buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();

#ifdef Q_OS_MACOS
  m_clearButton = new QPushButton(tr("Clear Binding (Cmd+D)"), this);
#else
  m_clearButton = new QPushButton(tr("Clear Binding (Ctrl+D)"), this);
#endif
  m_clearButton->setToolTip(tr("Remove the keybinding for the selected command"));
  connect(m_clearButton, &QPushButton::clicked, this, &KeybindingTab::onClearBinding);
  buttonLayout->addWidget(m_clearButton);

#ifdef Q_OS_MACOS
  m_resetButton = new QPushButton(tr("Reset to Defaults (Cmd+R)"), this);
#else
  m_resetButton = new QPushButton(tr("Reset to Defaults (Ctrl+R)"), this);
#endif
  m_resetButton->setToolTip(tr("Reset all keybindings to their default values"));
  connect(m_resetButton, &QPushButton::clicked, this, &KeybindingTab::onResetToDefaults);
  buttonLayout->addWidget(m_resetButton);

  buttonLayout->addStretch();

  mainLayout->addLayout(buttonLayout);
}

void KeybindingTab::loadKeybindings() {
  m_pendingChanges.clear();
  updateTable();

  // Select first row by default
  if (m_table->rowCount() > 0) {
    m_table->selectRow(0);
    m_table->setFocus();
  }
}

void KeybindingTab::updateTable() {
  m_table->setRowCount(0);
  m_table->setUpdatesEnabled(false);

  auto& registry   = CommandRegistry::instance();
  auto& keyManager = KeyBindingManager::instance();

  // 大見出し行（「本体」「ビュアー」）。カテゴリ見出しより強い配色・やや大きい
  // 文字で、本体（ファイルマネージャ）コマンド群とビュアーコマンド群の境目を
  // 明示する。3 列にまたがり、選択・編集はできない。
  auto addMajorHeading = [this](const QString& text) {
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    auto* item = new QTableWidgetItem(text);
    QFont f = item->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() + 1.0);
    item->setFont(f);
    // カテゴリ見出し (Mid + WindowText) より目立つよう、テーマのアクセント色
    // (選択色) を帯にして必ず可読な HighlightedText で描く。
    item->setBackground(palette().color(QPalette::Highlight));
    item->setForeground(palette().color(QPalette::HighlightedText));
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(Qt::ItemIsEnabled);  // 選択不可
    m_table->setItem(row, 0, item);
    m_table->setSpan(row, 0, 1, 3);
  };

  // ── 本体（ファイルマネージャ）のショートカット ──
  addMajorHeading(tr("Application"));

  // ショートカット一覧と並びを統一するため、CommandLayout のヘルパで
  // カテゴリ別 + 登録順にグルーピングしたものを使う。各グループの先頭に
  // 見出し行を 1 行ずつ挿入する。
  const auto groups = commandsGroupedByCategory(registry);
  for (const auto& group : groups) {
    // 見出し行 (3 列にまたがる)
    const int hdrRow = m_table->rowCount();
    m_table->insertRow(hdrRow);
    auto* hdr = new QTableWidgetItem(group.display);
    QFont f = hdr->font();
    f.setBold(true);
    hdr->setFont(f);
    hdr->setBackground(palette().color(QPalette::Mid));
    hdr->setForeground(palette().color(QPalette::WindowText));
    hdr->setFlags(Qt::ItemIsEnabled);  // 選択不可 (ダブルクリックでも編集に入らない)
    m_table->setItem(hdrRow, 0, hdr);
    m_table->setSpan(hdrRow, 0, 1, 3);

    // 各コマンド行 (登録順)
    for (ICommand* cmd : group.commands) {
      const int row = m_table->rowCount();
      m_table->insertRow(row);
      const QString commandId = cmd->id();

      // Command name
      QTableWidgetItem* nameItem = new QTableWidgetItem(cmd->label());
      nameItem->setData(Qt::UserRole, commandId);
      nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
      if (!cmd->description().isEmpty()) {
        nameItem->setToolTip(cmd->description());
      }
      m_table->setItem(row, 0, nameItem);

      // Current key
      QList<QKeySequence> keys = keyManager.keysForCommand(commandId);
      QString currentKey;
      if (!keys.isEmpty()) {
        currentKey = keySequenceToString(keys.first());
        if (keys.size() > 1) {
          currentKey += tr(" (and %1 more)").arg(keys.size() - 1);
        }
      }
      QTableWidgetItem* currentItem = new QTableWidgetItem(currentKey);
      currentItem->setFlags(currentItem->flags() & ~Qt::ItemIsEditable);
      m_table->setItem(row, 1, currentItem);

      // New key (pending changes)
      QString newKey;
      if (m_pendingChanges.contains(commandId)) {
        const QList<QKeySequence>& seqs = m_pendingChanges[commandId];
        if (seqs.isEmpty()) {
          newKey = tr("(unbound)");
        } else {
          newKey = keySequenceToString(seqs.first());
          if (seqs.size() > 1) {
            newKey += tr(" (and %1 more)").arg(seqs.size() - 1);
          }
        }
      }
      QTableWidgetItem* newItem = new QTableWidgetItem(newKey);
      newItem->setFlags(newItem->flags() & ~Qt::ItemIsEditable);
      if (!newKey.isEmpty()) {
        QFont font = newItem->font();
        font.setBold(true);
        newItem->setFont(font);
      }
      m_table->setItem(row, 2, newItem);
    }
  }

  // ── 同梱ビュアーのショートカット（ビュアー別セクション）──
  // 各ビュアーの「取得用共通 API」(shortcutCommands) から設定可能項目を集約し、
  // ビュアーごとに見出し + 行を並べる。編集対象は ViewerKeyBindingManager
  // （ビュアー内スコープ）。ここで付随情報 (viewerId / ラベル) を再構築する。
  m_viewerOfCommand.clear();
  m_viewerCmdLabel.clear();
  auto& viewerKeys = ViewerKeyBindingManager::instance();
  const QList<ViewerCommandDef> vdefs =
    ViewerDispatcher::instance().aggregatedShortcutCommands();
  // ── ビュアーのショートカット（「その他」の後に大見出しを挟む）──
  if (!vdefs.isEmpty()) {
    addMajorHeading(tr("Viewers"));
  }
  QString currentViewer;  // 直前に見出しを出した viewerId
  for (const ViewerCommandDef& def : vdefs) {
    if (def.viewerId != currentViewer) {
      currentViewer = def.viewerId;
      const int hdrRow = m_table->rowCount();
      m_table->insertRow(hdrRow);
      auto* hdr = new QTableWidgetItem(viewerSectionName(def.viewerId));
      QFont f = hdr->font();
      f.setBold(true);
      hdr->setFont(f);
      hdr->setBackground(palette().color(QPalette::Mid));
      hdr->setForeground(palette().color(QPalette::WindowText));
      hdr->setFlags(Qt::ItemIsEnabled);  // 選択不可
      m_table->setItem(hdrRow, 0, hdr);
      m_table->setSpan(hdrRow, 0, 1, 3);
    }
    m_viewerOfCommand.insert(def.commandId, def.viewerId);
    m_viewerCmdLabel.insert(def.commandId, def.label);

    const int row = m_table->rowCount();
    m_table->insertRow(row);

    auto* nameItem = new QTableWidgetItem(def.label);
    nameItem->setData(Qt::UserRole, def.commandId);
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    m_table->setItem(row, 0, nameItem);

    // Current key（ViewerKeyBindingManager 由来）
    const QList<QKeySequence> keys = viewerKeys.keysFor(def.commandId);
    QString currentKey;
    if (!keys.isEmpty()) {
      currentKey = keySequenceToString(keys.first());
      if (keys.size() > 1) {
        currentKey += tr(" (and %1 more)").arg(keys.size() - 1);
      }
    }
    auto* currentItem = new QTableWidgetItem(currentKey);
    currentItem->setFlags(currentItem->flags() & ~Qt::ItemIsEditable);
    m_table->setItem(row, 1, currentItem);

    // New key（ペンディング変更）
    QString newKey;
    if (m_pendingChanges.contains(def.commandId)) {
      const QList<QKeySequence>& seqs = m_pendingChanges[def.commandId];
      if (seqs.isEmpty()) {
        newKey = tr("(unbound)");
      } else {
        newKey = keySequenceToString(seqs.first());
        if (seqs.size() > 1) {
          newKey += tr(" (and %1 more)").arg(seqs.size() - 1);
        }
      }
    }
    auto* newItem = new QTableWidgetItem(newKey);
    newItem->setFlags(newItem->flags() & ~Qt::ItemIsEditable);
    if (!newKey.isEmpty()) {
      QFont font = newItem->font();
      font.setBold(true);
      newItem->setFont(font);
    }
    m_table->setItem(row, 2, newItem);
  }

  m_table->setUpdatesEnabled(true);
  // Don't call resizeColumnsToContents() as we have fixed column sizes
}

QString KeybindingTab::keySequenceToString(const QKeySequence& key) const {
  QString result = key.toString(QKeySequence::NativeText);

  // Replace platform-specific symbols with readable text
  result.replace("↖", "Home");
  result.replace("↘", "End");
  result.replace("⇞", "Page Up");
  result.replace("⇟", "Page Down");
  result.replace("←", "Left");
  result.replace("→", "Right");
  result.replace("↑", "Up");
  result.replace("↓", "Down");
  result.replace("⌫", "Backspace");
  result.replace("⌦", "Delete");
  result.replace("↩", "Return");
  result.replace("⇥", "Tab");
  result.replace("⎋", "Esc");

  return result;
}

void KeybindingTab::onCellDoubleClicked(int row, int column) {
  Q_UNUSED(column);

  if (row < 0 || row >= m_table->rowCount()) {
    return;
  }

  startRecording(row);
}

void KeybindingTab::startRecording(int row) {
  if (row < 0 || row >= m_table->rowCount()) {
    return;
  }

  QTableWidgetItem* nameItem = m_table->item(row, 0);
  if (!nameItem) {
    return;
  }

  m_editingCommandId = nameItem->data(Qt::UserRole).toString();
  // 見出し行 (UserRole 未設定 = 空) を編集対象にしない
  if (m_editingCommandId.isEmpty()) {
    return;
  }
  m_editingRow = row;
  m_inRecordMode = true;
  m_recordedKey = QKeySequence();

  // Get current key binding（ビュアーコマンドは ViewerKeyBindingManager から）
  QList<QKeySequence> keys;
  if (isViewerCommand(m_editingCommandId)) {
    keys = ViewerKeyBindingManager::instance().keysFor(m_editingCommandId);
  } else {
    keys = KeyBindingManager::instance().keysForCommand(m_editingCommandId);
  }
  QString currentKey = tr("(unbound)");
  if (!keys.isEmpty()) {
    currentKey = keySequenceToString(keys.first());
  }

  // Update record frame labels
  m_recordCommandLabel->setText(nameItem->text());
  m_recordCurrentKeyLabel->setText(currentKey);
  m_recordNewKeyLabel->setText(tr("<press a key>"));

  // Hide buttons and show record frame
  m_clearButton->setVisible(false);
  m_resetButton->setVisible(false);
  m_recordFrame->setVisible(true);
  m_recordFrame->setFocus();
}

bool KeybindingTab::eventFilter(QObject* obj, QEvent* event) {
  // Handle Enter key on table to start recording
  if (obj == m_table && event->type() == QEvent::KeyPress) {
    QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

    if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
      int row = m_table->currentRow();
      if (row >= 0) {
        startRecording(row);
        return true;
      }
    }

    // Let other keys pass through for normal table navigation
    return QWidget::eventFilter(obj, event);
  }

  // Handle key presses in the record frame
  if (obj == m_recordFrame && event->type() == QEvent::KeyPress && m_inRecordMode) {
    QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

    // Ignore modifier-only keys
    if (keyEvent->key() == Qt::Key_Control ||
        keyEvent->key() == Qt::Key_Shift ||
        keyEvent->key() == Qt::Key_Alt ||
        keyEvent->key() == Qt::Key_Meta) {
      return QWidget::eventFilter(obj, event);
    }

    // Confirm on Enter (without modifiers)
    if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
      if (keyEvent->modifiers() == Qt::NoModifier ||
          keyEvent->modifiers() == Qt::KeypadModifier) {
        // Validate and apply the recorded key
        if (!m_recordedKey.isEmpty()) {
          if (validateBinding(m_recordedKey, m_editingCommandId)) {
            m_pendingChanges[m_editingCommandId] = QList<QKeySequence>{ m_recordedKey };
            updateTable();
          }
        }
        // Hide record frame and show buttons
        m_recordFrame->setVisible(false);
        m_clearButton->setVisible(true);
        m_resetButton->setVisible(true);
        m_inRecordMode = false;
        m_recordedKey = QKeySequence();
        // Return focus to table
        m_table->setFocus();
        if (m_editingRow >= 0 && m_editingRow < m_table->rowCount()) {
          m_table->selectRow(m_editingRow);
        }
        return true;
      }
    }

    // Cancel on Cmd+C (macOS) or Ctrl+C (others), or Cmd+G / Ctrl+G
    // Note: On macOS, Qt maps Command key to ControlModifier by default
    if (keyEvent->key() == Qt::Key_C || keyEvent->key() == Qt::Key_G) {
      Qt::KeyboardModifiers mods = keyEvent->modifiers() & ~Qt::KeypadModifier;
      // On all platforms including macOS, check for ControlModifier
      // (on macOS, Command key is mapped to ControlModifier)
      if (mods == Qt::ControlModifier) {
        // Cancel recording
        m_recordFrame->setVisible(false);
        m_clearButton->setVisible(true);
        m_resetButton->setVisible(true);
        m_inRecordMode = false;
        m_recordedKey = QKeySequence();
        // Return focus to table
        m_table->setFocus();
        if (m_editingRow >= 0 && m_editingRow < m_table->rowCount()) {
          m_table->selectRow(m_editingRow);
        }
        return true;
      }
    }

    // Create key sequence
    int key = keyEvent->key();
    Qt::KeyboardModifiers modifiers = keyEvent->modifiers();

    // Remove the keypad modifier as it's not needed
    modifiers &= ~Qt::KeypadModifier;

    QKeySequence seq(key | modifiers);

    // Store the recorded key
    m_recordedKey = seq;

    // Update the label to show what was pressed
    m_recordNewKeyLabel->setText(keySequenceToString(seq));

    return true;
  }

  return QWidget::eventFilter(obj, event);
}

bool KeybindingTab::validateBinding(const QKeySequence& newKey, const QString& commandId) {
  if (newKey.isEmpty()) {
    return true;
  }

  // ── ビュアーコマンド: 競合検出は「同一ビュアー内」に限定する ──
  // 別ビュアーで同じキーを別機能に割り当てるのは許可 (ビュアー別スコープ)。
  if (isViewerCommand(commandId)) {
    const QString viewerId = m_viewerOfCommand.value(commandId);
    QString existingCommand =
      ViewerKeyBindingManager::instance().conflictCommand(viewerId, newKey, commandId);
    for (auto it = m_pendingChanges.begin(); it != m_pendingChanges.end(); ++it) {
      if (it.key() == commandId) {
        continue;
      }
      // 同一ビュアーのペンディングのみ競合対象。
      if (m_viewerOfCommand.value(it.key()) != viewerId) {
        continue;
      }
      if (it.value().contains(newKey)) {
        existingCommand = it.key();
        break;
      }
    }
    if (!existingCommand.isEmpty() && existingCommand != commandId) {
      const QString cmdLabel = m_viewerCmdLabel.value(existingCommand, existingCommand);
      if (!confirm(this, tr("Key Already Bound"),
                   tr("The key '%1' is already bound to '%2'.\n"
                      "Do you want to rebind it to this command?")
                      .arg(keySequenceToString(newKey), cmdLabel))) {
        return false;
      }
      m_pendingChanges[existingCommand] = QList<QKeySequence>{};
    }
    return true;
  }

  auto& keyManager = KeyBindingManager::instance();

  // Check if this key is already bound to another command
  QString existingCommand = keyManager.commandFor(newKey);

  // Check pending changes too. ペンディング側で同じキーを別コマンドに
  // 割り当てているなら衝突と見なす。ビュアーコマンドは別スコープなので除外。
  for (auto it = m_pendingChanges.begin(); it != m_pendingChanges.end(); ++it) {
    if (it.key() == commandId) continue;
    if (isViewerCommand(it.key())) continue;
    if (it.value().contains(newKey)) {
      existingCommand = it.key();
      break;
    }
  }

  if (!existingCommand.isEmpty() && existingCommand != commandId) {
    auto& registry = CommandRegistry::instance();
    ICommand* cmd = registry.command(existingCommand);
    QString cmdLabel = cmd ? cmd->label() : existingCommand;

    if (!confirm(this, tr("Key Already Bound"),
                 tr("The key '%1' is already bound to '%2'.\n"
                    "Do you want to rebind it to this command?")
                    .arg(keySequenceToString(newKey), cmdLabel))) {
      return false;
    }

    // Unbind the old command (空リスト = 全バインド削除)
    m_pendingChanges[existingCommand] = QList<QKeySequence>{};
  }

  return true;
}

void KeybindingTab::onClearBinding() {
  QList<QTableWidgetItem*> selected = m_table->selectedItems();
  if (selected.isEmpty()) {
    inform(this, tr("No Selection"),
                            tr("Please select a command to clear its binding."));
    return;
  }

  int row = m_table->currentRow();
  if (row < 0) {
    return;
  }

  QTableWidgetItem* nameItem = m_table->item(row, 0);
  if (!nameItem) {
    return;
  }

  QString commandId = nameItem->data(Qt::UserRole).toString();
  // 見出し行 (UserRole 未設定 = 空) は対象外
  if (commandId.isEmpty()) {
    return;
  }

  // 空リスト = 全バインド削除 (= unbound)
  m_pendingChanges[commandId] = QList<QKeySequence>{};

  updateTable();
}

void KeybindingTab::onResetToDefaults() {
  if (confirm(this, tr("Reset to Defaults"),
              tr("Are you sure you want to reset all keybindings to their "
                 "default values?\nThis will discard all custom keybindings."))) {
    m_pendingChanges.clear();

    // Load defaults temporarily to show in table
    auto& keyManager = KeyBindingManager::instance();
    keyManager.loadDefaults();
    // ビュアーのショートカットも既定へ戻す（メモリ上のみ。永続化は OK 時に
    // SettingsDialog が ViewerKeyBindingManager::saveToSettings() で行う）。
    ViewerKeyBindingManager::instance().resetAllToDefaults();

    updateTable();
  }
}

void KeybindingTab::onRecordOk() {
  if (!m_inRecordMode) {
    return;
  }

  // Validate and apply the recorded key
  if (!m_recordedKey.isEmpty()) {
    if (validateBinding(m_recordedKey, m_editingCommandId)) {
      m_pendingChanges[m_editingCommandId] = QList<QKeySequence>{ m_recordedKey };
      updateTable();
    }
  }


  // Hide record frame and show buttons
  m_recordFrame->setVisible(false);
  m_clearButton->setVisible(true);
  m_resetButton->setVisible(true);
  m_inRecordMode = false;
  m_recordedKey = QKeySequence();

  // Return focus to table
  m_table->setFocus();
  if (m_editingRow >= 0 && m_editingRow < m_table->rowCount()) {
    m_table->selectRow(m_editingRow);
  }
}

void KeybindingTab::onRecordCancel() {
  if (!m_inRecordMode) {
    return;
  }


  // Cancel recording
  m_recordFrame->setVisible(false);
  m_clearButton->setVisible(true);
  m_resetButton->setVisible(true);
  m_inRecordMode = false;
  m_recordedKey = QKeySequence();

  // Return focus to table
  m_table->setFocus();
  if (m_editingRow >= 0 && m_editingRow < m_table->rowCount()) {
    m_table->selectRow(m_editingRow);
  }
}

void KeybindingTab::save() {
  if (m_pendingChanges.isEmpty()) {
    return;
  }

  auto& keyManager = KeyBindingManager::instance();
  auto& viewerKeys = ViewerKeyBindingManager::instance();

  // 各 pending entry について: そのコマンドに割当てられている既存キーを
  // 全部 unbind してから、新しいキー列を 1 つずつ bind する。
  // 空リストなら新しい bind 無しで終わる (= unbound)。
  // ビュアーコマンドは ViewerKeyBindingManager (ビュアー別スコープ) へ振り分ける。
  for (auto it = m_pendingChanges.begin(); it != m_pendingChanges.end(); ++it) {
    const QString commandId          = it.key();
    const QList<QKeySequence>& newSeqs = it.value();

    if (isViewerCommand(commandId)) {
      // ビュアー側はコマンド単位でキー列を丸ごと置き換える。永続化と
      // 開いているビュアーへの再 push は SettingsDialog::onApply が
      // ViewerKeyBindingManager::saveToSettings() で行う。
      viewerKeys.setKeys(commandId, newSeqs);
      continue;
    }

    const QList<QKeySequence> oldKeys = keyManager.keysForCommand(commandId);
    for (const QKeySequence& oldKey : oldKeys) {
      keyManager.unbind(oldKey);
    }
    for (const QKeySequence& seq : newSeqs) {
      if (!seq.isEmpty()) keyManager.bind(seq, commandId);
    }
  }

  m_pendingChanges.clear();
}

void KeybindingTab::clearCurrentBinding() {
  onClearBinding();
}

void KeybindingTab::resetToDefaults() {
  onResetToDefaults();
}

bool KeybindingTab::applyBindingsToPending(const QJsonObject& obj, QString* errorMsg) {
  // ヘッダの軽い検証 (PresetIO::checkHeader と同等。ここでは type/version
  // チェックだけ行い、本体の bindings 配列を読み込む)。
  const QString type = obj.value(QStringLiteral("type")).toString();
  if (type != QLatin1String("farman.keybindings")) {
    if (errorMsg) {
      *errorMsg = tr("Wrong file type: expected 'farman.keybindings', got '%1'").arg(type);
    }
    return false;
  }
  if (!obj.value(QStringLiteral("bindings")).isArray()) {
    if (errorMsg) *errorMsg = tr("Invalid preset: missing 'bindings' array");
    return false;
  }

  // Step 1: 現在 KeyBindingManager に乗っている全コマンドを「空リスト」候補
  //         として pending に登録 (= JSON で出てこなかったら結果的に unbound)。
  m_pendingChanges.clear();
  auto& keyManager = KeyBindingManager::instance();
  const auto allBindings = keyManager.allBindings();
  QSet<QString> previouslyBound;
  for (auto it = allBindings.begin(); it != allBindings.end(); ++it) {
    previouslyBound.insert(it.value());
  }
  for (const QString& cmd : previouslyBound) {
    m_pendingChanges[cmd] = QList<QKeySequence>{};
  }

  // Step 2: JSON の各エントリで上書き
  const QJsonArray bindings = obj.value(QStringLiteral("bindings")).toArray();
  for (const QJsonValue& v : bindings) {
    if (!v.isObject()) continue;
    const QJsonObject e = v.toObject();
    const QString cmd = e.value(QStringLiteral("command")).toString();
    if (cmd.isEmpty()) continue;
    QList<QKeySequence> seqs;
    for (const QJsonValue& kv : e.value(QStringLiteral("keys")).toArray()) {
      const QString s = kv.toString();
      if (s.isEmpty()) continue;
      const QKeySequence seq(s);
      if (!seq.isEmpty()) seqs.append(seq);
    }
    m_pendingChanges[cmd] = seqs;
  }
  return true;
}

void KeybindingTab::onApplyPreset() {
  if (!m_presetCombo || m_presetCombo->currentIndex() < 0) return;
  const QString resourcePath = m_presetCombo->currentData().toString();
  // プレースホルダ "(Choose a preset...)" は data 空文字 → 何もせずペンディング解除
  if (resourcePath.isEmpty()) {
    m_pendingChanges.clear();
    updateTable();
    return;
  }

  QJsonObject obj;
  if (auto r = PresetIO::loadJsonFromResource(resourcePath, obj); !r.ok) {
    warn(this, tr("Apply Preset"), r.error);
    return;
  }
  QString err;
  if (!applyBindingsToPending(obj, &err)) {
    warn(this, tr("Apply Preset"), err);
    return;
  }
  updateTable();
}

void KeybindingTab::onExport() {
  // 現在 KeyBindingManager に乗っている (= 編集中含む) 状態をエクスポート。
  // ただし m_pendingChanges は KeyBindingManager に未反映なので、それも先に
  // 取り込んでからエクスポートしたい。… が、ここで m_pendingChanges に
  // コミットすると Cancel で戻せなくなる。簡素化のため「Export はディスクに
  // 保存済み + 編集中ペインの値」では無く「現在 KeyBindingManager に
  // 入っている値」をそのまま吐く方針 (= 個別編集分は OK 押下後にしか反映
  // されない)。気になる場合は OK → 再 Export してくださいというワークフロー。
  const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  const QString path = QFileDialog::getSaveFileName(
    this, tr("Export Keybindings"),
    defaultDir + QStringLiteral("/farman-keybindings.json"),
    tr("JSON Files (*.json)"));
  if (path.isEmpty()) return;

  if (auto r = PresetIO::exportKeybindingsToFile(path, /*name=*/{}); !r.ok) {
    warn(this, tr("Export Keybindings"), r.error);
    return;
  }
}

void KeybindingTab::onImport() {
  const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  const QString path = QFileDialog::getOpenFileName(
    this, tr("Import Keybindings"), defaultDir,
    tr("JSON Files (*.json)"));
  if (path.isEmpty()) return;

  if (!confirm(this, tr("Import Keybindings"),
               tr("Replace all current keybindings with those in this file?\n"
                  "Press OK in Settings to commit, or Cancel to revert."))) {
    return;
  }

  // ファイルを読んで applyBindingsToPending に流し込む。
  // (プリセット適用と同じく KeyBindingManager は触らず、テーブルの
  //  「New Key」列にプレビュー表示する)
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    warn(this, tr("Import Keybindings"),
                         tr("Cannot open file: %1").arg(file.errorString()));
    return;
  }
  const QByteArray bytes = file.readAll();
  file.close();
  QJsonParseError parseErr;
  const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseErr);
  if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
    warn(this, tr("Import Keybindings"),
                         tr("Invalid JSON: %1").arg(parseErr.errorString()));
    return;
  }
  QString err;
  if (!applyBindingsToPending(doc.object(), &err)) {
    warn(this, tr("Import Keybindings"), err);
    return;
  }
  updateTable();
}

} // namespace Farman
