#include "ShortcutListDialog.h"
#include "keybinding/CommandLayout.h"
#include "keybinding/CommandRegistry.h"
#include "keybinding/KeyBindingManager.h"
#include "settings/Settings.h"
#include "utils/EnterClickFilter.h"
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QShowEvent>
#include <QTableWidget>
#include <QVBoxLayout>

namespace Farman {

namespace {

QString keysToText(const QList<QKeySequence>& keys) {
  // 複数バインドがあるときは "C, Ctrl+C" のようにカンマ区切り。
  // ネイティブ表記を使うので macOS では ⌘C / Win/Linux では Ctrl+C 表示。
  // テンキー (Numpad) の Enter は Key_Return とペアで登録するのが慣例
  // (例: file.execute / view.choose) で表示が冗長になるので、一覧からは
  // 除外する。バインド自体は有効。
  QStringList parts;
  for (const auto& k : keys) {
    bool isNumpadEnter = false;
    for (int i = 0; i < k.count(); ++i) {
      if (k[i].key() == Qt::Key_Enter) { isNumpadEnter = true; break; }
    }
    if (isNumpadEnter) continue;

    QString s = k.toString(QKeySequence::NativeText);
    // macOS の NativeText は Home/End/PageUp/PageDown を斜め矢印で出して
    // 区別しづらいので、わかりやすい文字列に置換する。
    s.replace(QChar(0x2196), QStringLiteral("Home"));   // ↖
    s.replace(QChar(0x2198), QStringLiteral("End"));    // ↘
    s.replace(QChar(0x21DE), QStringLiteral("PgUp"));   // ⇞
    s.replace(QChar(0x21DF), QStringLiteral("PgDn"));   // ⇟

    if (!s.isEmpty()) parts << s;
  }
  return parts.isEmpty() ? QStringLiteral("—") : parts.join(QStringLiteral(", "));
}

} // namespace

ShortcutListDialog::ShortcutListDialog(QWidget* parent)
  : QDialog(parent) {
  // モードレスダイアログ。閉じるまで親ウィンドウの操作を妨げない。
  setWindowFlags(Qt::Tool | Qt::WindowCloseButtonHint | Qt::WindowTitleHint);
  setWindowTitle(tr("Keyboard Shortcuts"));
  setModal(false);
  resize(640, 720);
  setupUi();
  rebuild();

  // Settings で keybindings が変わったら追従
  connect(&Settings::instance(), &Settings::settingsChanged,
          this, &ShortcutListDialog::rebuild);
}

void ShortcutListDialog::setupUi() {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);

  // インクリメンタル検索ボックス。テキストが変わるたびに applyFilter で
  // テーブルを絞り込む。clearButton 付きで「×」一発で全件表示に戻せる。
  m_searchEdit = new QLineEdit(this);
  m_searchEdit->setPlaceholderText(tr("Filter (key, command name, or id)"));
  m_searchEdit->setClearButtonEnabled(true);
  connect(m_searchEdit, &QLineEdit::textChanged,
          this, &ShortcutListDialog::applyFilter);
  layout->addWidget(m_searchEdit);

  m_table = new QTableWidget(this);
  m_table->setStyleSheet(inactiveSelectionStyleSheet());
  m_table->setColumnCount(2);
  m_table->setHorizontalHeaderLabels({ tr("Key"), tr("Command") });
  m_table->verticalHeader()->setVisible(false);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setShowGrid(false);
  // カテゴリ見出し行に独自背景色を設定するため alternating は OFF
  // (alternating ON だとセル背景と競合してチラつく)
  m_table->setAlternatingRowColors(false);
  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
  m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_table->setColumnWidth(0, 150);
  layout->addWidget(m_table);
}

void ShortcutListDialog::rebuild() {
  if (!m_table) return;

  m_table->setRowCount(0);
  m_table->setUpdatesEnabled(false);

  const auto groups = commandsGroupedByCategory(CommandRegistry::instance());
  for (const auto& group : groups) {
    // 見出し行 (1 セルを 2 列にまたがらせる)
    const int hdrRow = m_table->rowCount();
    m_table->insertRow(hdrRow);
    auto* hdr = new QTableWidgetItem(group.display);
    QFont f = hdr->font();
    f.setBold(true);
    hdr->setFont(f);
    // 通常セルとはっきり差別化されるよう、Mid 系の濃いめの背景色を使う。
    // ライト/ダーク両テーマの palette に追従させる。
    hdr->setBackground(palette().color(QPalette::Mid));
    hdr->setForeground(palette().color(QPalette::WindowText));
    hdr->setFlags(Qt::ItemIsEnabled);  // 選択不可
    m_table->setItem(hdrRow, 0, hdr);
    m_table->setSpan(hdrRow, 0, 1, 2);

    // 各コマンド行 (登録順)
    for (ICommand* cmd : group.commands) {
      const int row = m_table->rowCount();
      m_table->insertRow(row);

      const auto keys = KeyBindingManager::instance().keysForCommand(cmd->id());
      // テンキー Enter のみのバインドは keysToText で除外されるため、
      // 「実質バインド無し」かどうかは表示文字列が "—" かで判定する。
      const QString keyText = keysToText(keys);
      const bool unbound    = (keyText == QStringLiteral("—"));

      auto* keyItem  = new QTableWidgetItem(keyText);
      auto* nameItem = new QTableWidgetItem(cmd->label());

      keyItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      keyItem->setToolTip(cmd->id());
      if (!cmd->description().isEmpty()) {
        nameItem->setToolTip(cmd->description());
      }

      // バインドが無いコマンドは、無効ボタンと同等の薄い色 (palette mid) で
      // 描画して「現状打鍵できない」ことを視認しやすくする。
      // ItemIsEnabled は外さない (= フィルタ検索やセル選択は普通に効く)。
      if (unbound) {
        const QColor dim = palette().color(QPalette::Disabled, QPalette::WindowText);
        keyItem->setForeground(dim);
        nameItem->setForeground(dim);
      }

      m_table->setItem(row, 0, keyItem);
      m_table->setItem(row, 1, nameItem);
    }
  }

  m_table->setUpdatesEnabled(true);

  // 直前の検索文字列が残っていれば再適用 (Settings 変更による rebuild 後に
  // 絞り込み状態が失われるのを防ぐ)。
  if (m_searchEdit) applyFilter(m_searchEdit->text());
}

void ShortcutListDialog::applyFilter(const QString& filter) {
  if (!m_table) return;

  const QString needle = filter.trimmed();
  const bool noFilter = needle.isEmpty();

  // 1 パス目: 各データ行を表示/非表示にする。同時に「直前のカテゴリ見出し
  // 行に対して 1 件でも表示行があったか」を覚えておく。見出し行は colspan
  // セル (列 0 のみ item を持つ) であり、データ行は列 0/1 両方に item を
  // 持つので、列 1 の有無で区別する。
  int   lastHeaderRow = -1;
  bool  headerHasVisibleChild = false;
  auto flushHeader = [&]() {
    if (lastHeaderRow >= 0) {
      m_table->setRowHidden(lastHeaderRow, !headerHasVisibleChild);
    }
  };

  const int rowCount = m_table->rowCount();
  for (int row = 0; row < rowCount; ++row) {
    QTableWidgetItem* col0 = m_table->item(row, 0);
    QTableWidgetItem* col1 = m_table->item(row, 1);
    if (!col0) continue;

    const bool isHeader = (col1 == nullptr);
    if (isHeader) {
      // 直前カテゴリの可視性を確定してから次のカテゴリへ
      flushHeader();
      lastHeaderRow = row;
      headerHasVisibleChild = false;
      continue;
    }

    bool match = noFilter;
    if (!noFilter) {
      // キー表記 / コマンド名 / コマンド ID (tooltip) のいずれかに含まれれば一致。
      const QString keyText = col0->text();
      const QString cmdName = col1->text();
      const QString cmdId   = col0->toolTip();
      const QString cmdDesc = col1->toolTip();
      match = keyText.contains(needle, Qt::CaseInsensitive)
           || cmdName.contains(needle, Qt::CaseInsensitive)
           || cmdId.contains(needle, Qt::CaseInsensitive)
           || cmdDesc.contains(needle, Qt::CaseInsensitive);
    }

    m_table->setRowHidden(row, !match);
    if (match) headerHasVisibleChild = true;
  }
  // 最後のカテゴリ分を反映
  flushHeader();
}

void ShortcutListDialog::showEvent(QShowEvent* event) {
  QDialog::showEvent(event);
  // 表示時は検索ボックスにフォーカス。`?` でトグル表示する想定なので、
  // 開いた直後にすぐタイプして絞り込みを始められるようにする。
  if (m_searchEdit) {
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
  }
}

void ShortcutListDialog::keyPressEvent(QKeyEvent* event) {
  // `?` でトグル閉じ、Esc でも閉じる。
  if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Question) {
    close();
    event->accept();
    return;
  }
  QDialog::keyPressEvent(event);
}

} // namespace Farman
