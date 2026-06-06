#include "BehaviorTab.h"
#include "settings/Settings.h"
#include "types.h"
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QToolButton>
#include <QFileDialog>
#include <QDir>
#include <QLabel>
#include <QStyle>

namespace Farman {

BehaviorTab::BehaviorTab(QWidget* parent)
  : QWidget(parent)
  , m_sortKeyCombo(nullptr)
  , m_sortOrderCombo(nullptr)
  , m_sortKey2ndCombo(nullptr)
  , m_sortDirsTypeCombo(nullptr)
  , m_sortDotFirstCheck(nullptr)
  , m_sortCaseSensitiveCheck(nullptr)
  , m_showHiddenCheck(nullptr)
  , m_cursorLoopCheck(nullptr)
  , m_typeAheadDotfilesCheck(nullptr)
  , m_persistHistoryCheck(nullptr)
  , m_autoRenameTemplateEdit(nullptr)
  , m_defaultDeleteToTrashCheck(nullptr)
  , m_searchExcludeDirsEdit(nullptr) {
  setupUi();
  loadSettings();
}

void BehaviorTab::setupUi() {
  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // Sort & Filter settings
  QGroupBox* sortGroup = new QGroupBox(tr("Default Sort && Filter Settings"), this);
  QVBoxLayout* sortGroupLayout = new QVBoxLayout(sortGroup);

  // Grid layout for sort controls (2x2 grid)
  QGridLayout* sortGrid = new QGridLayout();
  sortGrid->setColumnStretch(1, 1);
  sortGrid->setColumnStretch(3, 1);

  // Row 0: Sort by + Then by
  m_sortKeyCombo = new QComboBox(this);
  m_sortKeyCombo->addItem(tr("Name"), static_cast<int>(SortKey::Name));
  m_sortKeyCombo->addItem(tr("Size"), static_cast<int>(SortKey::Size));
  m_sortKeyCombo->addItem(tr("Type"), static_cast<int>(SortKey::Type));
  m_sortKeyCombo->addItem(tr("Last Modified"), static_cast<int>(SortKey::LastModified));
  m_sortKeyCombo->setToolTip(tr("Primary sort key for file lists"));
  sortGrid->addWidget(new QLabel(tr("Sort by:"), this), 0, 0);
  sortGrid->addWidget(m_sortKeyCombo, 0, 1);

  m_sortKey2ndCombo = new QComboBox(this);
  m_sortKey2ndCombo->addItem(tr("None"), static_cast<int>(SortKey::None));
  m_sortKey2ndCombo->addItem(tr("Name"), static_cast<int>(SortKey::Name));
  m_sortKey2ndCombo->addItem(tr("Size"), static_cast<int>(SortKey::Size));
  m_sortKey2ndCombo->addItem(tr("Type"), static_cast<int>(SortKey::Type));
  m_sortKey2ndCombo->addItem(tr("Last Modified"), static_cast<int>(SortKey::LastModified));
  m_sortKey2ndCombo->setToolTip(tr("Secondary sort key used when primary keys are equal"));
  sortGrid->addWidget(new QLabel(tr("Then by:"), this), 0, 2);
  sortGrid->addWidget(m_sortKey2ndCombo, 0, 3);

  // Row 1: Order + Directory Placement
  m_sortOrderCombo = new QComboBox(this);
  m_sortOrderCombo->addItem(tr("Ascending"), static_cast<int>(Qt::AscendingOrder));
  m_sortOrderCombo->addItem(tr("Descending"), static_cast<int>(Qt::DescendingOrder));
  m_sortOrderCombo->setToolTip(tr("Sort direction"));
  sortGrid->addWidget(new QLabel(tr("Order:"), this), 1, 0);
  sortGrid->addWidget(m_sortOrderCombo, 1, 1);

  m_sortDirsTypeCombo = new QComboBox(this);
  m_sortDirsTypeCombo->addItem(tr("Directories First"), static_cast<int>(SortDirsType::First));
  m_sortDirsTypeCombo->addItem(tr("Directories Last"), static_cast<int>(SortDirsType::Last));
  m_sortDirsTypeCombo->addItem(tr("Mixed with Files"), static_cast<int>(SortDirsType::Mixed));
  m_sortDirsTypeCombo->setToolTip(tr("Where to place directories in the sorted list"));
  sortGrid->addWidget(new QLabel(tr("Directory Placement:"), this), 1, 2);
  sortGrid->addWidget(m_sortDirsTypeCombo, 1, 3);

  sortGroupLayout->addLayout(sortGrid);

  // Checkboxes (Sort dot files / Case sensitive を 1 行に並べる)
  m_sortDotFirstCheck = new QCheckBox(tr("Sort dot files first"), this);
  m_sortDotFirstCheck->setToolTip(tr("Place files/folders starting with '.' at the beginning"));

  m_sortCaseSensitiveCheck = new QCheckBox(tr("Case sensitive sorting"), this);
  m_sortCaseSensitiveCheck->setToolTip(tr("Enable case-sensitive sorting (A-Z then a-z)"));

  QHBoxLayout* sortChecksRow = new QHBoxLayout();
  sortChecksRow->setContentsMargins(0, 0, 0, 0);
  sortChecksRow->addWidget(m_sortDotFirstCheck);
  sortChecksRow->addSpacing(16);
  sortChecksRow->addWidget(m_sortCaseSensitiveCheck);
  sortChecksRow->addStretch(1);
  sortGroupLayout->addLayout(sortChecksRow);

  m_showHiddenCheck = new QCheckBox(tr("Show hidden files by default"), this);
  m_showHiddenCheck->setToolTip(tr("Show files starting with '.' in file lists"));
  sortGroupLayout->addWidget(m_showHiddenCheck);

  mainLayout->addWidget(sortGroup);

  // Navigation settings
  QGroupBox* navigationGroup = new QGroupBox(tr("Navigation Settings"), this);
  QVBoxLayout* navigationLayout = new QVBoxLayout(navigationGroup);

  m_cursorLoopCheck = new QCheckBox(tr("Loop cursor between top and bottom"), this);
  m_cursorLoopCheck->setToolTip(
    tr("Pressing Down on the last row moves the cursor to the top, "
       "and Up on the first row moves it to the bottom"));

  m_persistHistoryCheck = new QCheckBox(tr("Persist directory history across sessions"), this);
  m_persistHistoryCheck->setToolTip(
    tr("Save each pane's recent directory list on exit and restore it on next launch"));

  m_typeAheadDotfilesCheck = new QCheckBox(
    tr("Match dotfile second character with Shift+key"), this);
  m_typeAheadDotfilesCheck->setToolTip(
    tr("When navigating with Shift+letter, match the character after the leading "
       "dot for files/directories starting with '.' (e.g. Shift+G jumps to .gitignore)"));

  m_syncBrowseShowDisabledDialogCheck = new QCheckBox(
    tr("Notify with a dialog when Sync Browse is auto-disabled"), this);
  m_syncBrowseShowDisabledDialogCheck->setToolTip(
    tr("If checked, show a dialog when Sync Browse is automatically turned off "
       "because the matching directory was not found in the other pane. "
       "This can also be controlled from the dialog itself ('Do not show this "
       "dialog next time')."));

  // 2 列均等幅の Grid に左寄せで並べる
  QGridLayout* navGrid = new QGridLayout();
  navGrid->setColumnStretch(0, 1);
  navGrid->setColumnStretch(1, 1);
  navGrid->addWidget(m_cursorLoopCheck,                     0, 0, Qt::AlignLeft);
  navGrid->addWidget(m_persistHistoryCheck,                 0, 1, Qt::AlignLeft);
  navGrid->addWidget(m_typeAheadDotfilesCheck,              1, 0, 1, 2, Qt::AlignLeft);
  navGrid->addWidget(m_syncBrowseShowDisabledDialogCheck,   2, 0, 1, 2, Qt::AlignLeft);
  navigationLayout->addLayout(navGrid);

  mainLayout->addWidget(navigationGroup);

  // File operation settings（auto-rename suffix と trash 既定を 2 列均等で横並び）
  QGroupBox* fileOpsGroup = new QGroupBox(tr("File Operations"), this);
  QGridLayout* fileOpsLayout = new QGridLayout(fileOpsGroup);
  fileOpsLayout->setColumnStretch(0, 1);
  fileOpsLayout->setColumnStretch(1, 1);

  m_autoRenameTemplateEdit = new QLineEdit(this);
  m_autoRenameTemplateEdit->setToolTip(
    tr("Default suffix template for auto-rename on copy/move conflicts. "
       "Use {n} as the counter placeholder (e.g., ' ({n})' → 'foo (1).txt')."));
  m_autoRenameTemplateEdit->setPlaceholderText(QStringLiteral(" ({n})"));
  m_autoRenameTemplateEdit->setMaximumWidth(160);

  // 左カラム: ラベル + 入力欄をまとめて 1 セルに
  QWidget* renameCell = new QWidget(this);
  QHBoxLayout* renameCellLayout = new QHBoxLayout(renameCell);
  renameCellLayout->setContentsMargins(0, 0, 0, 0);
  renameCellLayout->addWidget(new QLabel(tr("Auto-rename suffix:"), this));
  renameCellLayout->addWidget(m_autoRenameTemplateEdit);
  renameCellLayout->addStretch(1);

  // 「進捗ダイアログを自動で閉じる」を先に作る (Tab 順で先に来てほしいので)
  m_progressAutoCloseCheck = new QCheckBox(
    tr("Close progress dialog automatically when done"), this);
  m_progressAutoCloseCheck->setToolTip(
    tr("If on, the copy/move/delete progress dialog closes itself once the "
       "operation finishes. Off: keep the dialog open and let the user dismiss "
       "it. Can also be toggled directly from the progress dialog."));

  m_defaultDeleteToTrashCheck = new QCheckBox(tr("Move to Trash by default when deleting"), this);
  m_defaultDeleteToTrashCheck->setToolTip(
    tr("Pre-selects the Trash option in the delete confirmation dialog. "
       "Uncheck to default to permanent delete. The choice can still be "
       "overridden in the dialog per operation."));

  fileOpsLayout->addWidget(renameCell,                  0, 0, 1, 2, Qt::AlignLeft);
  fileOpsLayout->addWidget(m_progressAutoCloseCheck,    1, 0, Qt::AlignLeft);
  fileOpsLayout->addWidget(m_defaultDeleteToTrashCheck, 1, 1, Qt::AlignLeft);

  // Search 除外ディレクトリ（2 列をまたいで横長に）
  m_searchExcludeDirsEdit = new QLineEdit(this);
  m_searchExcludeDirsEdit->setToolTip(
    tr("Directory names (glob, space-separated) to skip when searching.\n"
       "Used as the initial value in the Search dialog's Exclude dirs field."));
  m_searchExcludeDirsEdit->setPlaceholderText(tr("e.g. .* node_modules build"));

  QWidget* excludeCell = new QWidget(this);
  QHBoxLayout* excludeCellLayout = new QHBoxLayout(excludeCell);
  excludeCellLayout->setContentsMargins(0, 0, 0, 0);
  excludeCellLayout->addWidget(new QLabel(tr("Search exclude dirs:"), this));
  excludeCellLayout->addWidget(m_searchExcludeDirsEdit, 1);

  fileOpsLayout->addWidget(excludeCell, 2, 0, 1, 2);

  mainLayout->addWidget(fileOpsGroup);

  // ─── List Display グループ: ファイルサイズ / 日時 の表示形式 ───
  // 装飾 (色やフォント) は Appearance タブ、こちらはデータの「見せ方」。
  // 2 画面 (Dual) と 1 画面 (Single) で別々の値を持てる。
  QGroupBox* listDisplayGroup = new QGroupBox(tr("List Display"), this);
  QVBoxLayout* listDisplayLayout = new QVBoxLayout(listDisplayGroup);

  auto makeSizeCombo = [this]() -> QComboBox* {
    QComboBox* c = new QComboBox(this);
    c->addItem(tr("Bytes"),              static_cast<int>(FileSizeFormat::Bytes));
    c->addItem(tr("KB/MB/GB (1000)"),    static_cast<int>(FileSizeFormat::SI));
    c->addItem(tr("KiB/MiB/GiB (1024)"), static_cast<int>(FileSizeFormat::IEC));
    c->addItem(tr("Auto"),               static_cast<int>(FileSizeFormat::Auto));
    c->setToolTip(tr("Choose how file sizes are displayed"));
    return c;
  };
  auto makeDateCombo = [this]() -> QComboBox* {
    QComboBox* c = new QComboBox(this);
    c->setEditable(true);
    c->addItem("yyyy/MM/dd HH:mm:ss");
    c->addItem("yyyy-MM-dd HH:mm:ss");
    c->addItem("dd/MM/yyyy HH:mm:ss");
    c->addItem("MM/dd/yyyy HH:mm:ss");
    c->addItem("yyyy/MM/dd");
    c->setToolTip(
      tr("Choose the date/time format. Use yyyy=year, MM=month, dd=day, "
         "HH=hour, mm=minute, ss=second."));
    return c;
  };

  // ─── 列表示 (Columns) ─────────────────────────
  // Name 列は常に表示で UI を持たない。
  const struct { const char* label; const char* tooltip; } colDefs[kColumnToggleCount] = {
    { QT_TR_NOOP("Type"),         QT_TR_NOOP("File extension / type") },
    { QT_TR_NOOP("Size"),         QT_TR_NOOP("File size") },
    { QT_TR_NOOP("Modified"),     QT_TR_NOOP("Last modified date/time") },
    { QT_TR_NOOP("Created"),      QT_TR_NOOP("Creation date/time (where supported)") },
    { QT_TR_NOOP("Permissions"),  QT_TR_NOOP("Unix permissions (rwxr-xr-x)") },
    { QT_TR_NOOP("Attributes"),   QT_TR_NOOP("File attribute flags (H=hidden, R=read-only, L=symlink)") },
    { QT_TR_NOOP("Owner"),        QT_TR_NOOP("File owner name (Unix)") },
    { QT_TR_NOOP("Group"),        QT_TR_NOOP("File group name (Unix)") },
    { QT_TR_NOOP("Link Target"),  QT_TR_NOOP("Resolved target of symbolic links") },
  };

  // モードごとのサブグループを構築するヘルパ。
  // 各サブグループに「ファイルサイズ」「日時」コンボ + 桁区切りチェック +
  // 列表示チェックボックス群をまとめる。
  // ウィジェット生成順 = Tab フォーカス順なので、グループ内では
  // 「サイズ → 日時 → 桁区切り → 列チェックボックス群」の順に作る。
  auto buildModeGroup = [&](const QString& title,
                            QComboBox*&  sizeComboOut,
                            QComboBox*&  dateComboOut,
                            QCheckBox*&  thousandsCheckOut,
                            QCheckBox**  columnChecks) -> QGroupBox* {
    QGroupBox* box = new QGroupBox(title, this);
    QVBoxLayout* boxLayout = new QVBoxLayout(box);

    // 1 行目: ファイルサイズ → 桁区切りカンマ → 日時 を横並び。
    // 生成順 = Tab 順 = 表示順。
    sizeComboOut = makeSizeCombo();
    thousandsCheckOut = new QCheckBox(tr("Comma"), this);
    thousandsCheckOut->setToolTip(
      tr("Insert thousands separators in the file size column. "
         "Only effective when the size format is 'Bytes'."));

    // SI/IEC/Auto では単位繰り上がりで数値が常に 0〜1023.99 に収まるので
    // カンマが入る余地が無い (実質的に Bytes のときしか効かない)。
    // 形式が Bytes 以外のときはチェックボックスを無効化して、ユーザーに
    // 「ここで切替えても変わらない」ことを視覚的に伝える。
    auto updateThousandsEnabled = [thousandsCheckOut, sizeComboOut]() {
      const FileSizeFormat fmt = static_cast<FileSizeFormat>(
        sizeComboOut->currentData().toInt());
      thousandsCheckOut->setEnabled(fmt == FileSizeFormat::Bytes);
    };
    QObject::connect(sizeComboOut,
                     QOverload<int>::of(&QComboBox::currentIndexChanged),
                     thousandsCheckOut, updateThousandsEnabled);
    updateThousandsEnabled();  // 初期状態 (loadSettings からの setCurrentIndex でも更新される)

    dateComboOut = makeDateCombo();

    QGridLayout* fmtGrid = new QGridLayout();
    fmtGrid->setHorizontalSpacing(12);
    fmtGrid->addWidget(new QLabel(tr("File Size:"), this), 0, 0, Qt::AlignLeft);
    fmtGrid->addWidget(sizeComboOut,                       0, 1);
    fmtGrid->addWidget(thousandsCheckOut,                  0, 2);
    fmtGrid->addWidget(new QLabel(tr("Date/Time:"), this), 0, 3, Qt::AlignLeft);
    fmtGrid->addWidget(dateComboOut,                       0, 4);
    fmtGrid->setColumnStretch(5, 1);
    boxLayout->addLayout(fmtGrid);

    // 3 行目: 列表示
    // プラットフォーム別に意味のない列はチェックボックスを隠す:
    //  - Windows: Permissions / Owner / Group は ACL ベースなのでここで
    //    rwx 形式に化けるだけで実用性が低いため非表示
    //  - macOS / Linux: Attributes は概ね Permissions と冗長なので非表示
    QHBoxLayout* colsRow = new QHBoxLayout();
    colsRow->setSpacing(12);

    // 先頭に Name を配置。常に ON で変更不可。
    QCheckBox* nameFixed = new QCheckBox(tr("Name"), this);
    nameFixed->setChecked(true);
    nameFixed->setEnabled(false);
    nameFixed->setToolTip(tr("File / directory name (always shown)"));
    colsRow->addWidget(nameFixed);

    for (int i = 0; i < kColumnToggleCount; ++i) {
      QCheckBox* c = new QCheckBox(tr(colDefs[i].label), this);
      c->setToolTip(tr(colDefs[i].tooltip));
      columnChecks[i] = c;
      // インデックスは colDefs と一致 (Type=0, Size=1, ..., LinkTarget=8)
#ifdef Q_OS_WIN
      // Windows: Permissions(idx 4) / Owner(idx 6) / Group(idx 7) を隠す
      if (i == 4 || i == 6 || i == 7) {
        c->setVisible(false);
        continue;
      }
#else
      // 非 Windows: Attributes(idx 5) を隠す
      if (i == 5) {
        c->setVisible(false);
        continue;
      }
#endif
      colsRow->addWidget(c);
    }
    colsRow->addStretch(1);
    boxLayout->addLayout(colsRow);

    return box;
  };

  // Dual グループを丸ごと作ってから Single グループを作る。
  // こうすると Dual 全フィールド → Single 全フィールド の順で Tab が回る。
  listDisplayLayout->addWidget(buildModeGroup(
    tr("Dual pane"),
    m_fileSizeFormatDualCombo,
    m_dateTimeFormatDualCombo,
    m_fileSizeThousandsSepDualCheck,
    m_listColumnDualCheck));
  listDisplayLayout->addWidget(buildModeGroup(
    tr("Single pane"),
    m_fileSizeFormatSingleCombo,
    m_dateTimeFormatSingleCombo,
    m_fileSizeThousandsSepSingleCheck,
    m_listColumnSingleCheck));

  // 念のため明示的に Tab 順を組む (各グループ内 + グループ間)。
  // size → date → thousands → cols(0..N-1)、最後に Single の先頭へ。
  auto chain = [](QWidget* a, QWidget* b) { QWidget::setTabOrder(a, b); };
  chain(m_fileSizeFormatDualCombo,        m_dateTimeFormatDualCombo);
  chain(m_dateTimeFormatDualCombo,        m_fileSizeThousandsSepDualCheck);
  chain(m_fileSizeThousandsSepDualCheck,  m_listColumnDualCheck[0]);
  for (int i = 0; i + 1 < kColumnToggleCount; ++i) {
    chain(m_listColumnDualCheck[i], m_listColumnDualCheck[i + 1]);
  }
  chain(m_listColumnDualCheck[kColumnToggleCount - 1], m_fileSizeFormatSingleCombo);
  chain(m_fileSizeFormatSingleCombo,        m_dateTimeFormatSingleCombo);
  chain(m_dateTimeFormatSingleCombo,        m_fileSizeThousandsSepSingleCheck);
  chain(m_fileSizeThousandsSepSingleCheck,  m_listColumnSingleCheck[0]);
  for (int i = 0; i + 1 < kColumnToggleCount; ++i) {
    chain(m_listColumnSingleCheck[i], m_listColumnSingleCheck[i + 1]);
  }

  mainLayout->addWidget(listDisplayGroup);

  mainLayout->addStretch();
}


void BehaviorTab::loadSettings() {
  auto& settings = Settings::instance();

  // Load default sort settings from left pane (as template)
  PaneSettings pane = settings.paneSettings(PaneType::Left);

  // Sort key
  for (int i = 0; i < m_sortKeyCombo->count(); ++i) {
    if (m_sortKeyCombo->itemData(i).toInt() == static_cast<int>(pane.sortKey)) {
      m_sortKeyCombo->setCurrentIndex(i);
      break;
    }
  }

  // Sort order
  for (int i = 0; i < m_sortOrderCombo->count(); ++i) {
    if (m_sortOrderCombo->itemData(i).toInt() == static_cast<int>(pane.sortOrder)) {
      m_sortOrderCombo->setCurrentIndex(i);
      break;
    }
  }

  // Sort key 2nd
  for (int i = 0; i < m_sortKey2ndCombo->count(); ++i) {
    if (m_sortKey2ndCombo->itemData(i).toInt() == static_cast<int>(pane.sortKey2nd)) {
      m_sortKey2ndCombo->setCurrentIndex(i);
      break;
    }
  }

  // Sort dirs type
  for (int i = 0; i < m_sortDirsTypeCombo->count(); ++i) {
    if (m_sortDirsTypeCombo->itemData(i).toInt() == static_cast<int>(pane.sortDirsType)) {
      m_sortDirsTypeCombo->setCurrentIndex(i);
      break;
    }
  }

  // Sort options
  m_sortDotFirstCheck->setChecked(pane.sortDotFirst);
  m_sortCaseSensitiveCheck->setChecked(pane.sortCS == Qt::CaseSensitive);

  // Display settings
  m_showHiddenCheck->setChecked(static_cast<bool>(pane.attrFilter & AttrFilter::ShowHidden));

  // Navigation settings
  m_cursorLoopCheck->setChecked(settings.cursorLoop());
  m_persistHistoryCheck->setChecked(settings.persistHistory());
  m_typeAheadDotfilesCheck->setChecked(settings.typeAheadIncludeDotfiles());
  m_syncBrowseShowDisabledDialogCheck->setChecked(settings.syncBrowseShowDisabledDialog());

  // File operations
  m_autoRenameTemplateEdit->setText(settings.autoRenameTemplate());
  m_defaultDeleteToTrashCheck->setChecked(settings.defaultDeleteToTrash());
  m_progressAutoCloseCheck->setChecked(settings.progressAutoClose());
  m_searchExcludeDirsEdit->setText(settings.searchExcludeDirs().join(QLatin1Char(' ')));

  // List display formats (Dual / Single)
  auto applySizeFormat = [](QComboBox* combo, FileSizeFormat fmt) {
    for (int i = 0; i < combo->count(); ++i) {
      if (combo->itemData(i).toInt() == static_cast<int>(fmt)) {
        combo->setCurrentIndex(i);
        return;
      }
    }
  };
  applySizeFormat(m_fileSizeFormatDualCombo,   settings.fileSizeFormatDual());
  applySizeFormat(m_fileSizeFormatSingleCombo, settings.fileSizeFormatSingle());
  m_fileSizeThousandsSepDualCheck->setChecked(settings.fileSizeThousandsSeparatorDual());
  m_fileSizeThousandsSepSingleCheck->setChecked(settings.fileSizeThousandsSeparatorSingle());
  m_dateTimeFormatDualCombo->setCurrentText(settings.dateTimeFormatDual());
  m_dateTimeFormatSingleCombo->setCurrentText(settings.dateTimeFormatSingle());

  // 列表示 (Dual / Single)
  const auto colsD = settings.listColumnVisibilityDual();
  const auto colsS = settings.listColumnVisibilitySingle();
  const bool dArr[kColumnToggleCount] = {
    colsD.type, colsD.size, colsD.lastModified, colsD.created,
    colsD.permissions, colsD.attributes, colsD.owner, colsD.group, colsD.linkTarget
  };
  const bool sArr[kColumnToggleCount] = {
    colsS.type, colsS.size, colsS.lastModified, colsS.created,
    colsS.permissions, colsS.attributes, colsS.owner, colsS.group, colsS.linkTarget
  };
  for (int i = 0; i < kColumnToggleCount; ++i) {
    if (m_listColumnDualCheck[i])   m_listColumnDualCheck[i]->setChecked(dArr[i]);
    if (m_listColumnSingleCheck[i]) m_listColumnSingleCheck[i]->setChecked(sArr[i]);
  }
}

void BehaviorTab::save() {
  auto& settings = Settings::instance();

  // Get current pane settings
  PaneSettings leftPane = settings.paneSettings(PaneType::Left);
  PaneSettings rightPane = settings.paneSettings(PaneType::Right);

  // Update sort settings for both panes
  SortKey sortKey = static_cast<SortKey>(m_sortKeyCombo->currentData().toInt());
  Qt::SortOrder sortOrder = static_cast<Qt::SortOrder>(m_sortOrderCombo->currentData().toInt());
  SortKey sortKey2nd = static_cast<SortKey>(m_sortKey2ndCombo->currentData().toInt());
  SortDirsType sortDirsType = static_cast<SortDirsType>(m_sortDirsTypeCombo->currentData().toInt());
  bool sortDotFirst = m_sortDotFirstCheck->isChecked();
  Qt::CaseSensitivity sortCS = m_sortCaseSensitiveCheck->isChecked() ?
                               Qt::CaseSensitive : Qt::CaseInsensitive;

  leftPane.sortKey = sortKey;
  leftPane.sortOrder = sortOrder;
  leftPane.sortKey2nd = sortKey2nd;
  leftPane.sortDirsType = sortDirsType;
  leftPane.sortDotFirst = sortDotFirst;
  leftPane.sortCS = sortCS;

  rightPane.sortKey = sortKey;
  rightPane.sortOrder = sortOrder;
  rightPane.sortKey2nd = sortKey2nd;
  rightPane.sortDirsType = sortDirsType;
  rightPane.sortDotFirst = sortDotFirst;
  rightPane.sortCS = sortCS;

  // Update display settings
  if (m_showHiddenCheck->isChecked()) {
    leftPane.attrFilter |= AttrFilter::ShowHidden;
    rightPane.attrFilter |= AttrFilter::ShowHidden;
  } else {
    leftPane.attrFilter &= ~static_cast<AttrFilterFlags>(AttrFilter::ShowHidden);
    rightPane.attrFilter &= ~static_cast<AttrFilterFlags>(AttrFilter::ShowHidden);
  }

  // Save pane settings
  settings.setPaneSettings(PaneType::Left, leftPane);
  settings.setPaneSettings(PaneType::Right, rightPane);

  // Save navigation settings
  settings.setCursorLoop(m_cursorLoopCheck->isChecked());
  settings.setPersistHistory(m_persistHistoryCheck->isChecked());
  settings.setTypeAheadIncludeDotfiles(m_typeAheadDotfilesCheck->isChecked());
  settings.setSyncBrowseShowDisabledDialog(m_syncBrowseShowDisabledDialogCheck->isChecked());

  // Save file operation settings
  settings.setAutoRenameTemplate(m_autoRenameTemplateEdit->text());
  settings.setDefaultDeleteToTrash(m_defaultDeleteToTrashCheck->isChecked());
  settings.setProgressAutoClose(m_progressAutoCloseCheck->isChecked());
  {
    const QStringList excludeList = m_searchExcludeDirsEdit->text().trimmed()
      .split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    settings.setSearchExcludeDirs(excludeList);
  }

  // Save list display formats (Dual / Single)
  settings.setFileSizeFormatDual(static_cast<FileSizeFormat>(
    m_fileSizeFormatDualCombo->currentData().toInt()));
  settings.setFileSizeFormatSingle(static_cast<FileSizeFormat>(
    m_fileSizeFormatSingleCombo->currentData().toInt()));
  settings.setFileSizeThousandsSeparatorDual(m_fileSizeThousandsSepDualCheck->isChecked());
  settings.setFileSizeThousandsSeparatorSingle(m_fileSizeThousandsSepSingleCheck->isChecked());
  settings.setDateTimeFormatDual(m_dateTimeFormatDualCombo->currentText());
  settings.setDateTimeFormatSingle(m_dateTimeFormatSingleCombo->currentText());

  // 列表示 (Dual / Single)
  Settings::ListColumnVisibility colsD;
  Settings::ListColumnVisibility colsS;
  bool* dArr[kColumnToggleCount] = {
    &colsD.type, &colsD.size, &colsD.lastModified, &colsD.created,
    &colsD.permissions, &colsD.attributes, &colsD.owner, &colsD.group, &colsD.linkTarget
  };
  bool* sArr[kColumnToggleCount] = {
    &colsS.type, &colsS.size, &colsS.lastModified, &colsS.created,
    &colsS.permissions, &colsS.attributes, &colsS.owner, &colsS.group, &colsS.linkTarget
  };
  for (int i = 0; i < kColumnToggleCount; ++i) {
    if (m_listColumnDualCheck[i])   *dArr[i] = m_listColumnDualCheck[i]->isChecked();
    if (m_listColumnSingleCheck[i]) *sArr[i] = m_listColumnSingleCheck[i]->isChecked();
  }
  settings.setListColumnVisibilityDual(colsD);
  settings.setListColumnVisibilitySingle(colsS);
}

} // namespace Farman
