#include "SortFilterDialog.h"
#include "utils/Dialogs.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QRegularExpression>

namespace Farman {

SortFilterDialog::SortFilterDialog(const QString& directoryPath,
                                   const PaneSettings& initial,
                                   bool initiallySaved,
                                   QWidget* parent)
  : QDialog(parent)
  , m_sortKeyCombo(nullptr)
  , m_sortOrderCombo(nullptr)
  , m_sortKey2ndCombo(nullptr)
  , m_sortDirsTypeCombo(nullptr)
  , m_sortDotFirstCheck(nullptr)
  , m_sortCaseSensitiveCheck(nullptr)
  , m_showHiddenCheck(nullptr)
  , m_dirsOnlyCheck(nullptr)
  , m_filesOnlyCheck(nullptr)
  , m_nameFiltersEdit(nullptr)
  , m_saveCheck(nullptr)
  , m_buttonBox(nullptr)
  , m_result(initial) {
  setupUi(directoryPath, initial, initiallySaved);
}

void SortFilterDialog::setupUi(const QString& directoryPath,
                               const PaneSettings& initial,
                               bool initiallySaved) {
  setWindowTitle(tr("Sort & Filter"));
  resize(520, 0);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // Directory path header
  QLabel* pathLabel = new QLabel(tr("Directory: %1").arg(directoryPath), this);
  pathLabel->setWordWrap(true);
  pathLabel->setStyleSheet("QLabel { font-weight: bold; padding: 4px; }");
  mainLayout->addWidget(pathLabel);

  // ── Alt ショートカットの方針 ───────────────────────────────
  // 検索ダイアログ等と同じく、入力行のラベルとチェックボックスに Alt+key を
  // 割り当てる。ラベルは altBuddyLabel、チェックは applyAltShortcut を通すこと
  // (macOS は '&' 由来の mnemonic が効かないので、明示的な setShortcut が要る)。
  // 割当: S 並び替え / T 次の条件 / R 昇順降順 / P ディレクトリの位置 /
  //       D ドットファイル / C 大文字小文字 / H 隠しファイル /
  //       I ディレクトリのみ / F ファイルのみ / N 名前フィルタ /
  //       V このディレクトリ専用として保存 / O OK / X キャンセル
  //
  // ── Sort group ──
  QGroupBox* sortGroup = new QGroupBox(tr("Sort"), this);
  QGridLayout* sortGrid = new QGridLayout(sortGroup);
  sortGrid->setColumnStretch(1, 1);
  sortGrid->setColumnStretch(3, 1);

  m_sortKeyCombo = new QComboBox(this);
  m_sortKeyCombo->addItem(tr("Name"),          static_cast<int>(SortKey::Name));
  m_sortKeyCombo->addItem(tr("Size"),          static_cast<int>(SortKey::Size));
  m_sortKeyCombo->addItem(tr("Type"),          static_cast<int>(SortKey::Type));
  m_sortKeyCombo->addItem(tr("Last Modified"), static_cast<int>(SortKey::LastModified));
  sortGrid->addWidget(
    altBuddyLabel(tr("Sort by:"), Qt::Key_S, m_sortKeyCombo, this), 0, 0);
  sortGrid->addWidget(m_sortKeyCombo, 0, 1);

  m_sortKey2ndCombo = new QComboBox(this);
  m_sortKey2ndCombo->addItem(tr("None"),          static_cast<int>(SortKey::None));
  m_sortKey2ndCombo->addItem(tr("Name"),          static_cast<int>(SortKey::Name));
  m_sortKey2ndCombo->addItem(tr("Size"),          static_cast<int>(SortKey::Size));
  m_sortKey2ndCombo->addItem(tr("Type"),          static_cast<int>(SortKey::Type));
  m_sortKey2ndCombo->addItem(tr("Last Modified"), static_cast<int>(SortKey::LastModified));
  sortGrid->addWidget(
    altBuddyLabel(tr("Then by:"), Qt::Key_T, m_sortKey2ndCombo, this), 0, 2);
  sortGrid->addWidget(m_sortKey2ndCombo, 0, 3);

  m_sortOrderCombo = new QComboBox(this);
  m_sortOrderCombo->addItem(tr("Ascending"),  static_cast<int>(Qt::AscendingOrder));
  m_sortOrderCombo->addItem(tr("Descending"), static_cast<int>(Qt::DescendingOrder));
  sortGrid->addWidget(
    altBuddyLabel(tr("Order:"), Qt::Key_R, m_sortOrderCombo, this), 1, 0);
  sortGrid->addWidget(m_sortOrderCombo, 1, 1);

  m_sortDirsTypeCombo = new QComboBox(this);
  m_sortDirsTypeCombo->addItem(tr("Directories First"), static_cast<int>(SortDirsType::First));
  m_sortDirsTypeCombo->addItem(tr("Directories Last"),  static_cast<int>(SortDirsType::Last));
  m_sortDirsTypeCombo->addItem(tr("Mixed with Files"),  static_cast<int>(SortDirsType::Mixed));
  sortGrid->addWidget(
    altBuddyLabel(tr("Directory Placement:"), Qt::Key_P, m_sortDirsTypeCombo, this),
    1, 2);
  sortGrid->addWidget(m_sortDirsTypeCombo, 1, 3);

  m_sortDotFirstCheck = new QCheckBox(tr("Sort dot files first"), this);
  applyAltShortcut(m_sortDotFirstCheck, Qt::Key_D);
  sortGrid->addWidget(m_sortDotFirstCheck, 2, 0, 1, 2);

  m_sortCaseSensitiveCheck = new QCheckBox(tr("Case sensitive"), this);
  applyAltShortcut(m_sortCaseSensitiveCheck, Qt::Key_C);
  sortGrid->addWidget(m_sortCaseSensitiveCheck, 2, 2, 1, 2);

  mainLayout->addWidget(sortGroup);

  // ── Filter group ──
  QGroupBox* filterGroup = new QGroupBox(tr("Filter"), this);
  QVBoxLayout* filterLayout = new QVBoxLayout(filterGroup);

  m_showHiddenCheck = new QCheckBox(tr("Show hidden files"), this);
  applyAltShortcut(m_showHiddenCheck, Qt::Key_H);
  filterLayout->addWidget(m_showHiddenCheck);

  QHBoxLayout* onlyLayout = new QHBoxLayout();
  m_dirsOnlyCheck  = new QCheckBox(tr("Directories only"), this);
  m_filesOnlyCheck = new QCheckBox(tr("Files only"),       this);
  applyAltShortcut(m_dirsOnlyCheck,  Qt::Key_I);
  applyAltShortcut(m_filesOnlyCheck, Qt::Key_F);
  // Dirs-only と Files-only は排他
  connect(m_dirsOnlyCheck, &QCheckBox::toggled, this, [this](bool checked) {
    if (checked) m_filesOnlyCheck->setChecked(false);
  });
  connect(m_filesOnlyCheck, &QCheckBox::toggled, this, [this](bool checked) {
    if (checked) m_dirsOnlyCheck->setChecked(false);
  });
  onlyLayout->addWidget(m_dirsOnlyCheck);
  onlyLayout->addWidget(m_filesOnlyCheck);
  onlyLayout->addStretch();
  filterLayout->addLayout(onlyLayout);

  QHBoxLayout* nameLayout = new QHBoxLayout();
  m_nameFiltersEdit = new QLineEdit(this);
  m_nameFiltersEdit->setPlaceholderText(tr("e.g. *.cpp *.h (space-separated)"));
  nameLayout->addWidget(
    altBuddyLabel(tr("Name filters:"), Qt::Key_N, m_nameFiltersEdit, this));
  nameLayout->addWidget(m_nameFiltersEdit, 1);
  filterLayout->addLayout(nameLayout);

  mainLayout->addWidget(filterGroup);

  // ── Save checkbox ──
  m_saveCheck = new QCheckBox(
    tr("Override defaults for this directory (save)"), this);
  applyAltShortcut(m_saveCheck, Qt::Key_V);
  m_saveCheck->setToolTip(
    tr("Save these settings as a per-directory override. They take priority over "
       "the Behavior tab defaults whenever this directory is opened. "
       "Unchecking a previously saved directory removes its override and falls "
       "back to the defaults."));
  mainLayout->addWidget(m_saveCheck);

  // ── Buttons ──
  m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  auto* okBtn     = m_buttonBox->button(QDialogButtonBox::Ok);
  auto* cancelBtn = m_buttonBox->button(QDialogButtonBox::Cancel);
  applyAltShortcut(okBtn,     Qt::Key_O);
  applyAltShortcut(cancelBtn, Qt::Key_X);
  // 誤操作防止のため実行系（OK）を Tab 順で最後に配置
  setTabOrder(cancelBtn, okBtn);
  connect(m_buttonBox, &QDialogButtonBox::accepted, this, &SortFilterDialog::onAccepted);
  connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(m_buttonBox);

  // macOS の System Settings → 「キーボードナビゲーション」設定に依存させずに
  // Tab キーで全ての操作対象を辿れるよう、対話ウィジェットに StrongFocus を
  // 明示する。
  const auto interactives = findChildren<QWidget*>();
  for (QWidget* w : interactives) {
    if (qobject_cast<QCheckBox*>(w)   || qobject_cast<QComboBox*>(w) ||
        qobject_cast<QLineEdit*>(w)   || qobject_cast<QPushButton*>(w)) {
      w->setFocusPolicy(Qt::StrongFocus);
    }
  }

  // Initialize from values
  auto selectByData = [](QComboBox* combo, int value) {
    for (int i = 0; i < combo->count(); ++i) {
      if (combo->itemData(i).toInt() == value) {
        combo->setCurrentIndex(i);
        return;
      }
    }
  };

  selectByData(m_sortKeyCombo,      static_cast<int>(initial.sortKey));
  selectByData(m_sortOrderCombo,    static_cast<int>(initial.sortOrder));
  selectByData(m_sortKey2ndCombo,   static_cast<int>(initial.sortKey2nd));
  selectByData(m_sortDirsTypeCombo, static_cast<int>(initial.sortDirsType));
  m_sortDotFirstCheck->setChecked(initial.sortDotFirst);
  m_sortCaseSensitiveCheck->setChecked(initial.sortCS == Qt::CaseSensitive);

  m_showHiddenCheck->setChecked(static_cast<bool>(initial.attrFilter & AttrFilter::ShowHidden));
  m_dirsOnlyCheck->setChecked(static_cast<bool>(initial.attrFilter & AttrFilter::DirsOnly));
  m_filesOnlyCheck->setChecked(static_cast<bool>(initial.attrFilter & AttrFilter::FilesOnly));

  m_nameFiltersEdit->setText(initial.nameFilters.join(' '));

  m_saveCheck->setChecked(initiallySaved);
}

void SortFilterDialog::onAccepted() {
  PaneSettings s = m_result;

  s.sortKey      = static_cast<SortKey>(m_sortKeyCombo->currentData().toInt());
  s.sortOrder    = static_cast<Qt::SortOrder>(m_sortOrderCombo->currentData().toInt());
  s.sortKey2nd   = static_cast<SortKey>(m_sortKey2ndCombo->currentData().toInt());
  s.sortDirsType = static_cast<SortDirsType>(m_sortDirsTypeCombo->currentData().toInt());
  s.sortDotFirst = m_sortDotFirstCheck->isChecked();
  s.sortCS       = m_sortCaseSensitiveCheck->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;

  AttrFilterFlags flags = AttrFilter::None;
  if (m_showHiddenCheck->isChecked()) flags |= AttrFilter::ShowHidden;
  if (m_dirsOnlyCheck->isChecked())   flags |= AttrFilter::DirsOnly;
  if (m_filesOnlyCheck->isChecked())  flags |= AttrFilter::FilesOnly;
  s.attrFilter = flags;

  QStringList filters;
  const auto parts = m_nameFiltersEdit->text().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
  for (const QString& p : parts) {
    filters.append(p);
  }
  s.nameFilters = filters;

  m_result = s;
  m_saveForDirectory = m_saveCheck->isChecked();

  accept();
}

} // namespace Farman
