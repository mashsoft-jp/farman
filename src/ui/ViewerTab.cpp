#include "ViewerTab.h"
#include "settings/Settings.h"
#include "viewer/IPluginSettingsPage.h"
#include "keybinding/ViewerKeyBindingManager.h"
#include "viewer/IViewerPlugin.h"
#include "viewer/ViewerDispatcher.h"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QKeyEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>

namespace Farman {

ViewerTab::ViewerTab(QWidget* parent)
  : QWidget(parent) {
  setupUi();
  loadSettings();
}

void ViewerTab::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);

  // ─── プラグイン一覧 (旧 Help → Plugins... ダイアログ) ───
  auto* listGroup = new QGroupBox(tr("Installed Plugins"), this);
  auto* listLayout = new QVBoxLayout(listGroup);

  auto* listHint = new QLabel(
    tr("Plugins are loaded on startup. Enable/disable changes take effect "
       "after restarting farman. The core viewer plugins (Text / Image / "
       "Binary / Media) are always enabled."),
    listGroup);
  listHint->setWordWrap(true);
  listLayout->addWidget(listHint);

  // 一覧は最低限の列 (有効 / 状態 / 名前 / 拡張子) に絞り、区分・プラグイン
  // ID・パス・エラー全文は各行右端の「詳細...」ボタンで開くダイアログで
  // 見せる。拡張子の紐付けの変更も詳細ダイアログで行う。エラーが起きている
  // 行は名前の前に警告アイコンを出して知らせる。
  m_pluginTable = new QTableWidget(listGroup);
  m_pluginTable->setWordWrap(false);  // 折り返さず省略 (…) で 1 行表示
  m_pluginTable->setColumnCount(6);
  m_pluginTable->setHorizontalHeaderLabels({
    tr("Enabled"),
    tr("Status"),
    tr("Name"),
    tr("Version"),
    tr("File Patterns"),
    QString()
  });
  m_pluginTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_pluginTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_pluginTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_pluginTable->verticalHeader()->setVisible(false);
  // Tab はセル間移動ではなく設定ダイアログの OK ボタンへ抜ける。テーブルに
  // フォーカスがある間の行移動は ↑/↓ のみで、Enter で詳細・Space で有効 /
  // 無効のトグル。制御は eventFilter で明示的に行う (ヘッダコメント参照)。
  m_pluginTable->setTabKeyNavigation(false);
  m_pluginTable->installEventFilter(this);
  updatePluginTablePalette(/*focused=*/false);  // 初期状態は非フォーカス

  listLayout->addWidget(m_pluginTable, 1);

  connect(m_pluginTable, &QTableWidget::itemDoubleClicked, this,
          [this](QTableWidgetItem* item) {
    if (item) showPluginDetails(item->row());
  });

  // 一覧のチェックボックス操作を編集状態へ反映する。
  connect(m_pluginTable, &QTableWidget::itemChanged, this,
          [this](QTableWidgetItem* item) {
    if (m_populating || !item || item->column() != 0) return;
    setPluginEnabled(item->row(), item->checkState() == Qt::Checked);
  });

  mainLayout->addWidget(listGroup, 1);
}

bool ViewerTab::eventFilter(QObject* watched, QEvent* event) {
  if (watched != m_pluginTable) return QWidget::eventFilter(watched, event);
  QTableWidget* table = m_pluginTable;

  // フォーカスを得たとき行未選択なら先頭行を選び、すぐ ↑/↓ で動かせるようにする。
  if (event->type() == QEvent::FocusIn) {
    updatePluginTablePalette(/*focused=*/true);
    if (table->currentRow() < 0 && table->rowCount() > 0) {
      table->selectRow(0);
    }
    return QWidget::eventFilter(watched, event);
  }
  if (event->type() == QEvent::FocusOut) {
    updatePluginTablePalette(/*focused=*/false);
    return QWidget::eventFilter(watched, event);
  }

  if (event->type() == QEvent::KeyPress) {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const int key = keyEvent->key();
    const bool backtab =
      key == Qt::Key_Backtab
      || (key == Qt::Key_Tab && (keyEvent->modifiers() & Qt::ShiftModifier));
    const bool tab =
      !backtab && key == Qt::Key_Tab && keyEvent->modifiers() == Qt::NoModifier;

    // Enter で詳細ダイアログ、Space で有効 / 無効のトグル。
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      showPluginDetails(table->currentRow());
      return true;
    }
    if (key == Qt::Key_Space) {
      const int row = table->currentRow();
      if (auto* item = (row >= 0) ? table->item(row, 0) : nullptr;
          item && (item->flags() & Qt::ItemIsUserCheckable)) {
        setPluginEnabled(row, item->checkState() != Qt::Checked);
      }
      return true;
    }
    // 行移動は ↑/↓ のみ。←/→ での現在セル移動はさせない
    if (key == Qt::Key_Left || key == Qt::Key_Right) {
      return true;
    }
    if (tab) {
      // 「詳細...」ボタンには止まらず、設定ダイアログの OK ボタンへ抜ける
      if (auto* buttonBox = window()->findChild<QDialogButtonBox*>()) {
        if (auto* okButton = buttonBox->button(QDialogButtonBox::Ok)) {
          okButton->setFocus(Qt::TabFocusReason);
          return true;
        }
      }
      return false;  // 見つからなければ既定のフォーカスチェーンに任せる
    }
    if (backtab) {
      return false;  // 既定のフォーカスチェーンに任せる
    }
  }
  return QWidget::eventFilter(watched, event);
}

void ViewerTab::loadSettings() {
  m_pluginRecords = ViewerDispatcher::instance().pluginRecords();
  // 優先度 (0 が最優先) の昇順に並べる。外部 (0〜9999) → PDF/CSV/Markdown
  // (10000) → コア (99996〜99999) の順になり、コア (固定) ビュアーが一番下に
  // 来る。優先度不明 (ロード失敗など) は最後尾。同点は登録順を保つ。
  std::stable_sort(m_pluginRecords.begin(), m_pluginRecords.end(),
                   [](const PluginRecord& lhs, const PluginRecord& rhs) {
    auto key = [](const PluginRecord& rec) {
      return rec.priority >= 0 ? rec.priority
                               : std::numeric_limits<int>::max();
    };
    return key(lhs) < key(rhs);
  });
  // ロード側 (Settings::isViewerPluginDisabled) は case-insensitive に判定
  // するので、UI 側の編集状態も小文字に正規化して持ち、判定を一致させる。
  m_disabledPluginIds.clear();
  for (const QString& id : Settings::instance().disabledViewerPlugins()) {
    m_disabledPluginIds.insert(id.trimmed().toLower());
  }
  loadExtensionState();  // 一覧の拡張子列が現在値を参照するので先に作る
  loadPluginList();

}

void ViewerTab::loadPluginList() {
  const QList<PluginRecord>& records = m_pluginRecords;

  // 行を作り直す間の itemChanged はユーザー操作ではないので無視する。
  m_populating = true;
  m_pluginTable->setRowCount(records.size());

  auto setItem = [this](int row, int col, const QString& text,
                        const QString& toolTip = QString()) {
    auto* item = new QTableWidgetItem(text);
    item->setToolTip(toolTip.isEmpty() ? text : toolTip);
    m_pluginTable->setItem(row, col, item);
  };

  for (int row = 0; row < records.size(); ++row) {
    const PluginRecord& rec = records[row];
    // コア (固定) ビュアー以外は同梱 / 外部を問わず切り替え可能。
    const bool toggleable =
      !rec.pluginId.isEmpty()
      && !ViewerDispatcher::isCoreViewerPlugin(rec.pluginId);
    // 有効 / 無効はこの一覧で直接切り替えられる (詳細ダイアログでも変更可)。
    // チェックは「ユーザーの有効 / 無効設定」そのもので、実際にロードできたか
    // は隣の状態列が示す (外部プラグイン読込みが OFF のときは、有効にして
    // あってもロードされない)。
    auto* enabledItem = new QTableWidgetItem();
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (toggleable) flags |= Qt::ItemIsUserCheckable;
    enabledItem->setFlags(flags);
    enabledItem->setCheckState(
      toggleable && isPluginDisabled(rec.pluginId) ? Qt::Unchecked : Qt::Checked);
    if (!toggleable) {
      enabledItem->setToolTip(rec.pluginId.isEmpty()
                                ? tr("Plugin ID is unavailable, so this plugin cannot be toggled.")
                                : tr("This core viewer plugin is always enabled."));
    } else if (rec.blockedExternalDisabled) {
      enabledItem->setToolTip(
        tr("External plugin loading is off. Turn on \"Allow loading external "
           "plugins\" in Settings → General to load this plugin."));
    } else {
      enabledItem->setToolTip(
        tr("Click to enable/disable. Takes effect after restarting farman."));
    }
    m_pluginTable->setItem(row, 0, enabledItem);

    // 状態は絵文字で表す。文言はツールチップと詳細ダイアログで見せる。
    auto* statusItem = new QTableWidgetItem(pluginStatusEmoji(rec));
    statusItem->setToolTip(pluginStatusText(rec));
    statusItem->setTextAlignment(Qt::AlignCenter);
    m_pluginTable->setItem(row, 1, statusItem);

    // 名前を取得できなかった失敗プラグインはファイル名で識別できるようにする。
    // エラーが起きている行は名前の前に警告アイコンを出して知らせる
    // (全文はツールチップと詳細ダイアログで見せる)。
    const QString name = rec.pluginName.isEmpty()
                           ? QFileInfo(rec.filePath).fileName()
                           : rec.pluginName;
    auto* nameItem = new QTableWidgetItem(name);
    if (rec.errorReason.isEmpty() || rec.disabledByUser
        || rec.blockedExternalDisabled) {
      // ユーザーによる無効化や外部読込み OFF はエラーではないので警告を出さない
      nameItem->setToolTip(name);
    } else {
      nameItem->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
      nameItem->setToolTip(tr("Error: %1").arg(rec.errorReason));
    }
    m_pluginTable->setItem(row, 2, nameItem);

    // 配布バージョン (取得できたもの)。同梱公式は farman 本体と同じ版数。
    setItem(row, 3, rec.version.isEmpty() ? QStringLiteral("-") : rec.version);

    // 紐付け中の拡張子 (現在値)。詳細ダイアログで編集すると更新される。
    // 編集対象外 (ID 不明) のプラグインは宣言された対応拡張子を見せる。
    setItem(row, 4, extensionsDisplayText(rec));

    auto* detailsButton = new QPushButton(tr("Details..."), m_pluginTable);
    detailsButton->setAutoDefault(false);
    detailsButton->setToolTip(tr("Show all information about this plugin."));
    // クリック専用。キーボードからは一覧の Enter / Space で開くので、
    // Tab フォーカスは受けない (一覧の Tab は OK ボタンへ抜ける)。
    detailsButton->setFocusPolicy(Qt::NoFocus);
    connect(detailsButton, &QPushButton::clicked, this, [this, row]() {
      m_pluginTable->selectRow(row);
      showPluginDetails(row);
    });
    m_pluginTable->setCellWidget(row, 5, detailsButton);
  }

  m_populating = false;

  m_pluginTable->resizeColumnsToContents();
  m_pluginTable->resizeRowsToContents();
  m_pluginTable->horizontalHeader()->setStretchLastSection(false);
  // 拡張子列 (4) を伸縮させる。詳細ボタン列 (5) はボタン幅に固定。
  m_pluginTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
  if (auto* button = m_pluginTable->cellWidget(0, 5)) {
    m_pluginTable->setColumnWidth(5, button->sizeHint().width() + 8);
  }
}
// 一覧の「拡張子」列に出す文字列。編集中の現在値を優先し、編集対象外の
// プラグインはレコードが宣言する対応拡張子にフォールバックする。
QString ViewerTab::extensionsDisplayText(const PluginRecord& record) const {
  if (!record.pluginId.isEmpty() && m_extensions.contains(record.pluginId)) {
    return m_extensions.value(record.pluginId).join(QStringLiteral(", "));
  }
  return record.supportedExtensions.join(QStringLiteral(", "));
}

// 一覧の選択行 (カーソル) の色をフォーカス状態に合わせる。
// 項目ビューの描画はウィンドウのアクティブ状態しか見ないため、テーブルが
// フォーカスを失っただけでは選択色が変わらない。非フォーカス時は Highlight
// を非アクティブ用のグレーに差し替えて、カーソルが効いていないことを示す。
void ViewerTab::updatePluginTablePalette(bool focused) {
  if (focused) {
    m_pluginTable->setPalette(QPalette());  // 既定 (親のパレット) に戻す
    return;
  }
  QPalette pal;
  QColor inactive = pal.color(QPalette::Inactive, QPalette::Highlight);
  if (inactive == pal.color(QPalette::Active, QPalette::Highlight)) {
    // OS パレットが非アクティブ用の色を持たない場合のフォールバック
    inactive = pal.color(QPalette::Active, QPalette::Window).darker(115);
  }
  pal.setColor(QPalette::Highlight, inactive);
  pal.setColor(QPalette::HighlightedText,
               pal.color(QPalette::Active, QPalette::Text));
  m_pluginTable->setPalette(pal);
}

// 一覧のチェックボックス / 詳細ダイアログのどちらから変更しても、同じ編集
// 状態 (m_disabledPluginIds) と同じ表示 (一覧のチェック) を更新する。
void ViewerTab::setPluginEnabled(int row, bool enabled) {
  if (row < 0 || row >= m_pluginRecords.size()) return;
  const QString pluginId = m_pluginRecords[row].pluginId.trimmed();
  if (pluginId.isEmpty()) return;

  if (enabled) {
    m_disabledPluginIds.remove(pluginId.toLower());
  } else {
    m_disabledPluginIds.insert(pluginId.toLower());
  }

  if (auto* item = m_pluginTable->item(row, 0)) {
    const Qt::CheckState state = enabled ? Qt::Checked : Qt::Unchecked;
    if (item->checkState() != state) {
      const bool wasPopulating = m_populating;
      m_populating = true;   // 表示合わせの setCheckState を再入させない
      item->setCheckState(state);
      m_populating = wasPopulating;
    }
  }
}

QString ViewerTab::pluginStatusText(const PluginRecord& record) const {
  if (record.loaded) return tr("Loaded");
  if (record.blockedExternalDisabled) return tr("Blocked (external plugins off)");
  return record.disabledByUser ? tr("Disabled") : tr("Failed");
}

QString ViewerTab::pluginStatusEmoji(const PluginRecord& record) const {
  if (record.loaded) return QStringLiteral("✅");
  if (record.blockedExternalDisabled) return QStringLiteral("🔒");
  return record.disabledByUser ? QStringLiteral("🚫") : QStringLiteral("❌");
}

void ViewerTab::showPluginDetails(int row) {
  if (row < 0 || row >= m_pluginRecords.size()) return;
  const PluginRecord& rec = m_pluginRecords[row];

  QDialog dialog(this);
  dialog.setWindowTitle(tr("Plugin Details"));

  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  auto addField = [&dialog, form](const QString& label, const QString& value) {
    auto* valueLabel = new QLabel(value.isEmpty() ? QStringLiteral("-") : value,
                                  &dialog);
    valueLabel->setWordWrap(true);
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(label, valueLabel);
  };

  // 有効 / 無効の切り替え (一覧は表示のみで、変更はここで行う)。
  // コア (固定) ビュアーと ID 不明のプラグインは切り替え不可。
  const bool enabledEditable =
    !rec.pluginId.isEmpty()
    && !ViewerDispatcher::isCoreViewerPlugin(rec.pluginId);
  auto* enabledCheck = new QCheckBox(&dialog);
  enabledCheck->setChecked(
    !(enabledEditable && isPluginDisabled(rec.pluginId)));
  enabledCheck->setEnabled(enabledEditable);
  if (enabledEditable) {
    enabledCheck->setToolTip(tr("Changes take effect after restarting farman."));
  } else {
    enabledCheck->setToolTip(rec.pluginId.isEmpty()
                               ? tr("Plugin ID is unavailable, so this plugin cannot be toggled.")
                               : tr("This core viewer plugin is always enabled."));
  }
  form->addRow(tr("Enabled:"), enabledCheck);

  addField(tr("Type:"), tr("Viewer"));
  addField(tr("Status:"), pluginStatusEmoji(rec) + QStringLiteral(" ")
                            + pluginStatusText(rec));
  // 優先度 (0 が最優先)。取得できなかった場合は "-"。
  addField(tr("Priority:"),
           rec.priority >= 0 ? QString::number(rec.priority) : QString());
  addField(tr("Origin:"), rec.origin == PluginRecord::Origin::Bundled
                            ? tr("Bundled")
                            : tr("External"));
  addField(tr("Plugin ID:"), rec.pluginId);
  addField(tr("Name:"), rec.pluginName);
  addField(tr("Version:"), rec.version);
  addField(tr("Author:"), rec.author);
  // 制作者 URL はリンクとして表示し、クリックで既定ブラウザを開く。
  if (!rec.authorUrl.isEmpty()) {
    auto* urlLabel = new QLabel(
      QStringLiteral("<a href=\"%1\">%1</a>")
        .arg(rec.authorUrl.toHtmlEscaped()), &dialog);
    urlLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    urlLabel->setOpenExternalLinks(true);
    urlLabel->setWordWrap(true);
    form->addRow(tr("Author URL:"), urlLabel);
  }

  // ロード済みプラグインの本体ポインタ。設定ページの有無や拡張子を自前管理
  // するか (managesOwnExtensions) の判定に使う。
  IViewerPlugin* plugin =
    (rec.loaded && !rec.pluginId.isEmpty())
      ? ViewerDispatcher::instance().pluginById(rec.pluginId)
      : nullptr;
  const bool pluginManagesExtensions = plugin && plugin->managesOwnExtensions();

  // 拡張子の紐付け: ビュアープラグインはここで確認・変更できる。
  // 値がプラグイン既定の拡張子と一致している間は既定に追従する。
  // ID 不明 (ロード失敗など) のプラグインは編集できないので、プラグインが
  // 宣言する対応拡張子を表示するだけにとどめる。
  // 拡張子を自前管理するプラグインは、下の「設定」ページ内に拡張子欄を持つ
  // ので、ここ (ホスト側) には出さない。
  QLineEdit* extensionsEdit = nullptr;
  const bool extensionsEditable =
    !pluginManagesExtensions
    && !rec.pluginId.isEmpty() && m_extensions.contains(rec.pluginId);
  if (pluginManagesExtensions) {
    // 何も出さない (設定ページが担当)。
  } else if (extensionsEditable) {
    extensionsEdit = new QLineEdit(&dialog);
    extensionsEdit->setText(
      m_extensions.value(rec.pluginId).join(QStringLiteral(", ")));
    extensionsEdit->setPlaceholderText(tr("mp4, *.tar.gz, Makefile"));
    extensionsEdit->setToolTip(
      tr("Comma, semicolon, or space separated patterns. Write an extension "
         "(mp4), a glob (*.tar.gz), or a whole file name (Makefile)."));
    form->addRow(tr("File patterns:"), extensionsEdit);
  } else {
    addField(tr("File patterns:"),
             rec.supportedExtensions.join(QStringLiteral(", ")));
  }

  addField(tr("Path:"), rec.filePath);
  if (!rec.errorReason.isEmpty()) {
    addField(tr("Error:"), rec.errorReason);
  }
  layout->addLayout(form);

  // 設定 UI を持つロード済みプラグインは、設定ページを詳細ダイアログ内に直接
  // 埋め込む (別ウィンドウにしない)。項目数が少ないので 1 枚で完結させる。
  IPluginSettingsPage* settingsPage = nullptr;
  if (plugin && plugin->hasSettings()) {
    auto* group = new QGroupBox(tr("Settings"), &dialog);
    auto* groupLayout = new QVBoxLayout(group);
    settingsPage = plugin->createSettingsPage(group);
    if (settingsPage) {
      groupLayout->addWidget(settingsPage);
      layout->addWidget(group);
    } else {
      delete group;
    }
  }

  // 設定ページがある場合も OK/Cancel を出す (Apply で確定 / Cancel で破棄)。
  const bool editable = extensionsEditable || enabledEditable || settingsPage;
  auto* buttons = new QDialogButtonBox(
    editable ? (QDialogButtonBox::Ok | QDialogButtonBox::Cancel)
             : QDialogButtonBox::StandardButtons(QDialogButtonBox::Close),
    &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  // 設定ページがあるときだけ「既定に戻す」を出す。
  if (settingsPage) {
    auto* rd = buttons->addButton(QDialogButtonBox::RestoreDefaults);
    connect(rd, &QPushButton::clicked, settingsPage,
            [settingsPage]() { settingsPage->restoreDefaults(); });
  }
  layout->addWidget(buttons);

  // パスが長いと初期幅が画面いっぱいまで伸びるので適度に抑える。
  dialog.resize(std::clamp(dialog.sizeHint().width(), 420, 560),
                dialog.sizeHint().height());
  if (dialog.exec() != QDialog::Accepted) return;

  // 設定ページを永続化し、本体 Settings を再読込して開いているビュアーへ反映。
  if (settingsPage) {
    settingsPage->save();
    Settings::instance().load();
  }

  if (enabledEditable) {
    // 一覧のチェックボックスと同じ経路で反映する。
    setPluginEnabled(row, enabledCheck->isChecked());
  }
  if (extensionsEdit) {
    m_extensions[rec.pluginId] = normalizedExtensions(extensionsEdit->text());
    // 一覧の「拡張子」列 (col 4) にも編集後の値を反映する
    if (auto* item = m_pluginTable->item(row, 4)) {
      const QString text = extensionsDisplayText(rec);
      item->setText(text);
      item->setToolTip(text);
    }
  }
}

QString ViewerTab::normalizedExtension(const QString& extension) const {
  QString result = extension.trimmed().toLower();
  while (result.startsWith(QLatin1Char('.'))) {
    result.remove(0, 1);
  }
  return result;
}

QStringList ViewerTab::normalizedExtensions(const QString& text) const {
  QSet<QString> extensions;
  const QStringList parts = text.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                       Qt::SkipEmptyParts);
  for (const QString& part : parts) {
    const QString normalized = normalizedExtension(part);
    if (!normalized.isEmpty()) {
      extensions.insert(normalized);
    }
  }
  QStringList result = extensions.values();
  result.sort(Qt::CaseInsensitive);
  return result;
}

QStringList ViewerTab::defaultExtensionsForPlugin(IViewerPlugin* plugin) const {
  return plugin ? defaultExtensionsFromList(plugin->supportedExtensions()) : QStringList();
}

QStringList ViewerTab::defaultExtensionsFromList(const QStringList& source) const {
  QStringList extensions;
  for (const QString& ext : source) {
    const QString normalized = normalizedExtension(ext);
    if (!normalized.isEmpty() && !extensions.contains(normalized)) {
      extensions.append(normalized);
    }
  }
  extensions.sort(Qt::CaseInsensitive);
  return extensions;
}

// 拡張子紐付けの編集状態を Settings から組み立てる。
// 旧「Viewer Associations」テーブルと同じ規則:
//   - 明示的な紐付けがあるプラグインはそれを現在値とする
//   - 無ければプラグイン既定の拡張子 (他へ明示割り当て済みのものは除く) を
//     現在値とし、既定値と一致している間は「既定に追従」とみなす
// 値の編集は詳細ダイアログ (showPluginDetails) で行い、save() で既定と
// 異なるものだけを Settings に書き戻す。
void ViewerTab::loadExtensionState() {
  m_extensionOrder.clear();
  m_extensions.clear();
  m_extensionDefaults.clear();
  m_preservedPatterns.clear();

  const QMap<QString, QStringList> overrides =
    Settings::instance().viewerFilePatterns();

  auto addEntry = [&](const QString& pluginId, QStringList defaults) {
    if (pluginId.isEmpty() || m_extensions.contains(pluginId)) return;
    // 設定に上書きがあればそれを、無ければプラグインの既定を見せる。
    const QStringList overridden = normalizedExtensions(
      overrides.value(pluginId).join(QStringLiteral(", ")));
    m_extensionOrder.append(pluginId);
    m_extensions.insert(pluginId, overridden.isEmpty() ? defaults : overridden);
    m_extensionDefaults.insert(pluginId, defaults);
  };

  // save() の競合解決 (同じ拡張子は先勝ち) の優先順は旧テーブルの行順を
  // 踏襲する: 外部 (ロード済) → 無効化済み外部 → 同梱 → その他のレコード。
  QSet<QString> externalPluginIds;
  QMap<QString, PluginRecord> disabledPluginRecords;
  for (const PluginRecord& record : m_pluginRecords) {
    if (record.loaded
        && record.origin == PluginRecord::Origin::External
        && !record.pluginId.isEmpty()) {
      externalPluginIds.insert(record.pluginId);
    } else if (!record.loaded
               && !record.pluginId.isEmpty()
               && record.disabledByUser) {
      // 無効化されたプラグイン (同梱 / 外部とも)。紐付けは編集可能なまま残す
      disabledPluginRecords.insert(record.pluginId, record);
    }
  }

  QList<IViewerPlugin*> plugins = ViewerDispatcher::instance().allPlugins();
  std::stable_sort(plugins.begin(), plugins.end(),
                   [&externalPluginIds](IViewerPlugin* lhs, IViewerPlugin* rhs) {
    const bool lhsExternal = lhs && externalPluginIds.contains(lhs->pluginId());
    const bool rhsExternal = rhs && externalPluginIds.contains(rhs->pluginId());
    return lhsExternal && !rhsExternal;
  });

  auto addDisabledEntries = [&]() {
    QStringList ids = disabledPluginRecords.keys();
    ids.sort(Qt::CaseInsensitive);
    for (const QString& pluginId : ids) {
      addEntry(pluginId,
               defaultExtensionsFromList(
                 disabledPluginRecords.value(pluginId).supportedExtensions));
    }
  };

  bool disabledInserted = false;
  for (IViewerPlugin* plugin : plugins) {
    if (!plugin) continue;
    if (!disabledInserted && !externalPluginIds.contains(plugin->pluginId())) {
      addDisabledEntries();
      disabledInserted = true;
    }
    addEntry(plugin->pluginId(), defaultExtensionsForPlugin(plugin));
  }
  if (!disabledInserted) {
    addDisabledEntries();
  }

  // ロード失敗などで上に出てこなかったレコードも、ID が分かるなら
  // 詳細ダイアログから編集できるようにする。
  for (const PluginRecord& record : m_pluginRecords) {
    addEntry(record.pluginId,
             defaultExtensionsFromList(record.supportedExtensions));
  }

  // 一覧に存在しないプラグインのパターンは編集できないので、そのまま保持して
  // save() で書き戻す (プラグインを戻したとき設定が残るように)。
  for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
    if (!m_extensions.contains(it.key())) {
      m_preservedPatterns.insert(it.key(), it.value());
    }
  }
}

void ViewerTab::save() {
  auto& settings = Settings::instance();
  m_restartRequiredOnSave = false;

  // プラグインの有効 / 無効 (次回起動から有効)。編集は詳細ダイアログで
  // 行い、ここでは一覧に出ているプラグインの分だけ書き換える。
  // コア (固定) ビュアーは常に有効なので、設定に残っていても取り除く。
  QStringList disabled = settings.disabledViewerPlugins();
  for (const PluginRecord& rec : m_pluginRecords) {
    if (rec.pluginId.isEmpty()) continue;
    // ロード側の判定 (case-insensitive) に合わせ、大小違いの残留も取り除く
    disabled.removeIf([&rec](const QString& id) {
      return id.trimmed().compare(rec.pluginId, Qt::CaseInsensitive) == 0;
    });
    if (!ViewerDispatcher::isCoreViewerPlugin(rec.pluginId)
        && isPluginDisabled(rec.pluginId)) {
      disabled.append(rec.pluginId);
    }
  }
  disabled.sort(Qt::CaseInsensitive);
  disabled.removeDuplicates();
  if (disabled != settings.disabledViewerPlugins()) {
    settings.setDisabledViewerPlugins(disabled);
    m_restartRequiredOnSave = true;
  }

  // ビュアーの対象ファイルパターン (詳細ダイアログで編集した値)。
  // 既定に追従しているプラグインは書かず、明示的に変えたものだけ保存する。
  QMap<QString, QStringList> patterns;
  for (const QString& pluginId : m_extensionOrder) {
    // パターンを自前管理するプラグインは、ホスト側のこの設定ではなく自分の
    // Settings キー (supportedExtensions 経由) で解決するのでここでは書かない。
    if (IViewerPlugin* p = ViewerDispatcher::instance().pluginById(pluginId);
        p && p->managesOwnExtensions()) {
      continue;
    }
    const QStringList current = m_extensions.value(pluginId);
    if (current == m_extensionDefaults.value(pluginId)) continue;
    if (!current.isEmpty()) patterns.insert(pluginId, current);
  }
  // 一覧に出ないプラグインのぶんはそのまま戻す。
  for (auto it = m_preservedPatterns.cbegin(); it != m_preservedPatterns.cend(); ++it) {
    if (!patterns.contains(it.key())) patterns.insert(it.key(), it.value());
  }
  settings.setViewerFilePatterns(patterns);
}

} // namespace Farman
