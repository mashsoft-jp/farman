#include "PluginsTab.h"
#include "settings/Settings.h"
#include "viewer/IViewerPlugin.h"
#include "viewer/ViewerDispatcher.h"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
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
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace Farman {

PluginsTab::PluginsTab(QWidget* parent)
  : QWidget(parent) {
  setupUi();
  loadSettings();
}

void PluginsTab::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);

  // ─── プラグインディレクトリ (旧 General → Viewer Plugins) ───
  auto* dirGroup = new QGroupBox(tr("Plugins Directory"), this);
  auto* dirLayout = new QVBoxLayout(dirGroup);

  auto* dirHint = new QLabel(
    tr("External plugins are loaded on startup from this directory. "
       "Leave empty to use the default user plugins directory."), dirGroup);
  dirHint->setWordWrap(true);
  dirLayout->addWidget(dirHint);

  auto* dirRow = new QWidget(dirGroup);
  auto* dirRowLayout = new QHBoxLayout(dirRow);
  dirRowLayout->setContentsMargins(0, 0, 0, 0);

  m_pluginsDirectoryEdit = new QLineEdit(dirGroup);
  m_pluginsDirectoryEdit->setPlaceholderText(Settings::defaultPluginsDirectory());
  m_pluginsDirectoryEdit->setToolTip(
    tr("Directory containing external viewer plugins (.dylib, .so, .dll). "
       "Changes take effect on next launch."));
  m_pluginsDirectoryBrowse = new QToolButton(dirGroup);
  m_pluginsDirectoryBrowse->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
  m_pluginsDirectoryBrowse->setToolTip(tr("Choose plugins directory..."));
  m_pluginsDirectoryDefault = new QToolButton(dirGroup);
  m_pluginsDirectoryDefault->setText(tr("Default"));
  m_pluginsDirectoryDefault->setToolTip(
    tr("Use the default user plugins directory."));

  dirRowLayout->addWidget(new QLabel(tr("Directory:"), dirGroup));
  dirRowLayout->addWidget(m_pluginsDirectoryEdit, 1);
  dirRowLayout->addWidget(m_pluginsDirectoryBrowse);
  dirRowLayout->addWidget(m_pluginsDirectoryDefault);
  dirLayout->addWidget(dirRow);

  connect(m_pluginsDirectoryBrowse, &QToolButton::clicked, this, [this]() {
    const QString start = m_pluginsDirectoryEdit->text().isEmpty()
                          ? Settings::defaultPluginsDirectory()
                          : m_pluginsDirectoryEdit->text();
    const QString selected = QFileDialog::getExistingDirectory(
      this, tr("Choose plugins directory"), start,
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!selected.isEmpty()) {
      m_pluginsDirectoryEdit->setText(selected);
    }
  });
  connect(m_pluginsDirectoryDefault, &QToolButton::clicked,
          m_pluginsDirectoryEdit, &QLineEdit::clear);

  mainLayout->addWidget(dirGroup);

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

  // 種別ごとにタブで出し分ける。現状はビュアープラグインのみなので
  // 「Viewer」タブだけ。将来 Content / FS / Archive 等の種別が増えたら
  // タブを追加する (ヘッダコメント参照)。
  m_pluginTabs = new QTabWidget(listGroup);

  // 一覧は最低限の列 (有効 / 状態 / 名前 / 拡張子) に絞り、区分・プラグイン
  // ID・パス・エラー全文は各行右端の「詳細...」ボタンで開くダイアログで
  // 見せる。拡張子の紐付けの変更も詳細ダイアログで行う。エラーが起きている
  // 行は名前の前に警告アイコンを出して知らせる。
  m_pluginTable = new QTableWidget(m_pluginTabs);
  m_pluginTable->setColumnCount(5);
  m_pluginTable->setHorizontalHeaderLabels({
    tr("Enabled"),
    tr("Status"),
    tr("Name"),
    tr("Extensions"),
    QString()
  });
  m_pluginTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_pluginTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_pluginTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_pluginTable->verticalHeader()->setVisible(false);
  // Tab はセル間移動ではなく設定ダイアログの OK ボタンへ抜ける。テーブルに
  // フォーカスがある間の行移動は ↑/↓ のみで、Enter / Space で詳細を開く。
  // 制御は eventFilter で明示的に行う (ヘッダコメント参照)。
  m_pluginTable->setTabKeyNavigation(false);
  m_pluginTable->installEventFilter(this);
  updatePluginTablePalette(/*focused=*/false);  // 初期状態は非フォーカス
  m_pluginTabs->addTab(m_pluginTable, tr("Viewer"));
  listLayout->addWidget(m_pluginTabs, 1);

  connect(m_pluginTable, &QTableWidget::itemDoubleClicked, this,
          [this](QTableWidgetItem* item) {
    if (item) showPluginDetails(item->row());
  });

  mainLayout->addWidget(listGroup, 1);
}

bool PluginsTab::eventFilter(QObject* watched, QEvent* event) {
  if (watched != m_pluginTable) return QWidget::eventFilter(watched, event);

  // フォーカスを得たとき行未選択なら先頭行を選び、すぐ ↑/↓ で動かせるようにする。
  if (event->type() == QEvent::FocusIn) {
    updatePluginTablePalette(/*focused=*/true);
    if (m_pluginTable->currentRow() < 0 && m_pluginTable->rowCount() > 0) {
      m_pluginTable->selectRow(0);
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

    // Enter / Space で選択行の詳細ダイアログを開く
    if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) {
      showPluginDetails(m_pluginTable->currentRow());
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
      m_pluginsDirectoryEdit->setFocus(Qt::BacktabFocusReason);
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

void PluginsTab::loadSettings() {
  m_pluginsDirectoryEdit->setText(Settings::instance().pluginsDirectory());
  m_pluginRecords = ViewerDispatcher::instance().pluginRecords();
  // 切り替えできるプラグインを上、コア (固定) ビュアーを下に並べる。
  // 各グループ内は登録順を保つ。
  std::stable_partition(m_pluginRecords.begin(), m_pluginRecords.end(),
                        [](const PluginRecord& rec) {
    return !ViewerDispatcher::isCoreViewerPlugin(rec.pluginId);
  });
  const QStringList disabled = Settings::instance().disabledViewerPlugins();
  m_disabledPluginIds = QSet<QString>(disabled.cbegin(), disabled.cend());
  loadExtensionState();  // 一覧の拡張子列が現在値を参照するので先に作る
  loadPluginList();
}

void PluginsTab::loadPluginList() {
  const QList<PluginRecord>& records = m_pluginRecords;

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
    // 有効 / 無効は表示のみ (ItemIsUserCheckable を付けない)。
    // 切り替えは詳細ダイアログで行う。
    auto* enabledItem = new QTableWidgetItem();
    enabledItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    enabledItem->setCheckState(
      toggleable && m_disabledPluginIds.contains(rec.pluginId)
        ? Qt::Unchecked
        : Qt::Checked);
    if (toggleable) {
      enabledItem->setToolTip(
        tr("Enable/disable can be changed in the Details dialog."));
    } else {
      enabledItem->setToolTip(rec.pluginId.isEmpty()
                                ? tr("Plugin ID is unavailable, so this plugin cannot be toggled.")
                                : tr("This core viewer plugin is always enabled."));
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
    if (rec.errorReason.isEmpty() || rec.disabledByUser) {
      // ユーザーによる無効化はエラーではないので警告アイコンは出さない
      nameItem->setToolTip(name);
    } else {
      nameItem->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
      nameItem->setToolTip(tr("Error: %1").arg(rec.errorReason));
    }
    m_pluginTable->setItem(row, 2, nameItem);

    // 紐付け中の拡張子 (現在値)。詳細ダイアログで編集すると更新される。
    // 編集対象外 (ID 不明) のプラグインは宣言された対応拡張子を見せる。
    setItem(row, 3, extensionsDisplayText(rec));

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
    m_pluginTable->setCellWidget(row, 4, detailsButton);
  }

  m_pluginTable->resizeColumnsToContents();
  m_pluginTable->resizeRowsToContents();
  m_pluginTable->horizontalHeader()->setStretchLastSection(false);
  m_pluginTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  if (auto* button = m_pluginTable->cellWidget(0, 4)) {
    m_pluginTable->setColumnWidth(4, button->sizeHint().width() + 8);
  }
}

// 一覧の「拡張子」列に出す文字列。編集中の現在値を優先し、編集対象外の
// プラグインはレコードが宣言する対応拡張子にフォールバックする。
QString PluginsTab::extensionsDisplayText(const PluginRecord& record) const {
  if (!record.pluginId.isEmpty() && m_extensions.contains(record.pluginId)) {
    return m_extensions.value(record.pluginId).join(QStringLiteral(", "));
  }
  return record.supportedExtensions.join(QStringLiteral(", "));
}

// 一覧の選択行 (カーソル) の色をフォーカス状態に合わせる。
// 項目ビューの描画はウィンドウのアクティブ状態しか見ないため、テーブルが
// フォーカスを失っただけでは選択色が変わらない。非フォーカス時は Highlight
// を非アクティブ用のグレーに差し替えて、カーソルが効いていないことを示す。
void PluginsTab::updatePluginTablePalette(bool focused) {
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

QString PluginsTab::pluginStatusText(const PluginRecord& record) const {
  if (record.loaded) return tr("Loaded");
  return record.disabledByUser ? tr("Disabled") : tr("Failed");
}

QString PluginsTab::pluginStatusEmoji(const PluginRecord& record) const {
  if (record.loaded) return QStringLiteral("✅");
  return record.disabledByUser ? QStringLiteral("🚫") : QStringLiteral("❌");
}

void PluginsTab::showPluginDetails(int row) {
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
    !(enabledEditable && m_disabledPluginIds.contains(rec.pluginId)));
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
  addField(tr("Origin:"), rec.origin == PluginRecord::Origin::Bundled
                            ? tr("Bundled")
                            : tr("External"));
  addField(tr("Plugin ID:"), rec.pluginId);
  addField(tr("Name:"), rec.pluginName);

  // 拡張子の紐付け: ビュアープラグインはここで確認・変更できる。
  // 値がプラグイン既定の拡張子と一致している間は既定に追従する。
  // ID 不明 (ロード失敗など) のプラグインは編集できないので、プラグインが
  // 宣言する対応拡張子を表示するだけにとどめる。
  QLineEdit* extensionsEdit = nullptr;
  const bool extensionsEditable =
    !rec.pluginId.isEmpty() && m_extensions.contains(rec.pluginId);
  if (extensionsEditable) {
    extensionsEdit = new QLineEdit(&dialog);
    extensionsEdit->setText(
      m_extensions.value(rec.pluginId).join(QStringLiteral(", ")));
    extensionsEdit->setPlaceholderText(tr("mp4, mkv"));
    extensionsEdit->setToolTip(
      tr("Comma, semicolon, or space separated extensions without leading dots."));
    form->addRow(tr("Extensions:"), extensionsEdit);
  } else {
    addField(tr("Extensions:"),
             rec.supportedExtensions.join(QStringLiteral(", ")));
  }

  addField(tr("Path:"), rec.filePath);
  if (!rec.errorReason.isEmpty()) {
    addField(tr("Error:"), rec.errorReason);
  }
  layout->addLayout(form);

  const bool editable = extensionsEditable || enabledEditable;
  auto* buttons = new QDialogButtonBox(
    editable ? (QDialogButtonBox::Ok | QDialogButtonBox::Cancel)
             : QDialogButtonBox::StandardButtons(QDialogButtonBox::Close),
    &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);

  // パスが長いと初期幅が画面いっぱいまで伸びるので適度に抑える。
  dialog.resize(std::clamp(dialog.sizeHint().width(), 420, 560),
                dialog.sizeHint().height());
  if (dialog.exec() != QDialog::Accepted) return;

  if (enabledEditable) {
    if (enabledCheck->isChecked()) {
      m_disabledPluginIds.remove(rec.pluginId);
    } else {
      m_disabledPluginIds.insert(rec.pluginId);
    }
    // 一覧の「有効」列にも編集後の状態を反映する
    if (auto* item = m_pluginTable->item(row, 0)) {
      item->setCheckState(enabledCheck->isChecked() ? Qt::Checked
                                                    : Qt::Unchecked);
    }
  }
  if (extensionsEdit) {
    m_extensions[rec.pluginId] = normalizedExtensions(extensionsEdit->text());
    // 一覧の「拡張子」列にも編集後の値を反映する
    if (auto* item = m_pluginTable->item(row, 3)) {
      const QString text = extensionsDisplayText(rec);
      item->setText(text);
      item->setToolTip(text);
    }
  }
}

QString PluginsTab::normalizedExtension(const QString& extension) const {
  QString result = extension.trimmed().toLower();
  while (result.startsWith(QLatin1Char('.'))) {
    result.remove(0, 1);
  }
  return result;
}

QStringList PluginsTab::normalizedExtensions(const QString& text) const {
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

QString PluginsTab::extensionsTextForPlugin(
  const QMap<QString, QString>& associations,
  const QString& pluginId) const {
  QStringList extensions;
  for (auto it = associations.cbegin(); it != associations.cend(); ++it) {
    if (it.value() == pluginId) {
      extensions.append(it.key());
    }
  }
  extensions.sort(Qt::CaseInsensitive);
  return extensions.join(QStringLiteral(", "));
}

QStringList PluginsTab::defaultExtensionsForPlugin(IViewerPlugin* plugin) const {
  return plugin ? defaultExtensionsFromList(plugin->supportedExtensions()) : QStringList();
}

QStringList PluginsTab::defaultExtensionsFromList(const QStringList& source) const {
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
void PluginsTab::loadExtensionState() {
  m_extensionOrder.clear();
  m_extensions.clear();
  m_extensionDefaults.clear();
  m_preservedAssociations.clear();

  const QMap<QString, QString> associations =
    Settings::instance().viewerAssociations();
  QSet<QString> assignedExtensions;
  for (auto it = associations.cbegin(); it != associations.cend(); ++it) {
    assignedExtensions.insert(it.key());
  }

  auto addEntry = [&](const QString& pluginId, QStringList defaults) {
    if (pluginId.isEmpty() || m_extensions.contains(pluginId)) return;
    const QStringList explicitExtensions =
      normalizedExtensions(extensionsTextForPlugin(associations, pluginId));
    QStringList visible = explicitExtensions;
    if (visible.isEmpty()) {
      // 他のプラグインへ明示割り当て済みの拡張子は既定から除いて見せる
      for (int i = defaults.size() - 1; i >= 0; --i) {
        if (assignedExtensions.contains(defaults.at(i))) {
          defaults.removeAt(i);
        }
      }
      visible = defaults;
    }
    m_extensionOrder.append(pluginId);
    m_extensions.insert(pluginId, visible);
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

  // 一覧に存在しないプラグインへの紐付けは編集できないので、そのまま
  // 保持して save() で書き戻す (プラグインを戻したとき設定が残るように)。
  for (auto it = associations.cbegin(); it != associations.cend(); ++it) {
    if (!m_extensions.contains(it.value())) {
      m_preservedAssociations.insert(it.key(), it.value());
    }
  }
}

void PluginsTab::save() {
  auto& settings = Settings::instance();
  m_restartRequiredOnSave = false;

  // プラグインディレクトリ (次回起動から有効)
  const QString newDirectory = m_pluginsDirectoryEdit->text().trimmed();
  if (newDirectory != settings.pluginsDirectory()) {
    settings.setPluginsDirectory(newDirectory);
    m_restartRequiredOnSave = true;
  }

  // プラグインの有効 / 無効 (次回起動から有効)。編集は詳細ダイアログで
  // 行い、ここでは一覧に出ているプラグインの分だけ書き換える。
  // コア (固定) ビュアーは常に有効なので、設定に残っていても取り除く。
  QStringList disabled = settings.disabledViewerPlugins();
  for (const PluginRecord& rec : m_pluginRecords) {
    if (rec.pluginId.isEmpty()) continue;
    disabled.removeAll(rec.pluginId);
    if (!ViewerDispatcher::isCoreViewerPlugin(rec.pluginId)
        && m_disabledPluginIds.contains(rec.pluginId)) {
      disabled.append(rec.pluginId);
    }
  }
  disabled.sort(Qt::CaseInsensitive);
  disabled.removeDuplicates();
  if (disabled != settings.disabledViewerPlugins()) {
    settings.setDisabledViewerPlugins(disabled);
    m_restartRequiredOnSave = true;
  }

  // ビュアーの拡張子紐付け (詳細ダイアログで編集した値)。
  // 既定に追従しているプラグインは書かず、明示的に変えたものだけ保存する。
  QMap<QString, QString> associations;
  for (const QString& pluginId : m_extensionOrder) {
    const QStringList extensions = m_extensions.value(pluginId);
    if (extensions == m_extensionDefaults.value(pluginId)) {
      continue;
    }
    for (const QString& extension : extensions) {
      if (!associations.contains(extension)) {
        associations.insert(extension, pluginId);
      }
    }
  }
  // 一覧に出ないプラグインの紐付けは最後に (最低優先で) そのまま戻す。
  for (auto it = m_preservedAssociations.cbegin();
       it != m_preservedAssociations.cend(); ++it) {
    if (!associations.contains(it.key())) {
      associations.insert(it.key(), it.value());
    }
  }
  settings.setViewerAssociations(associations);
}

} // namespace Farman
