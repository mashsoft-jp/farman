#include "SearchDialog.h"
#include "core/workers/SearchWorker.h"
#include "settings/Settings.h"
#include "utils/Dialogs.h"
#include "utils/EnterClickFilter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QDateTimeEdit>
#include <QDate>
#include <QTime>
#include <QPushButton>
#include <QToolButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QFileDialog>
#include <QFileInfo>
#include <QDateTime>
#include <QLocale>
#include <QStyle>
#include <QKeyEvent>
#include <QKeySequence>

namespace Farman {

namespace {

QString humanSize(qint64 bytes) {
  return QLocale::system().formattedDataSize(bytes);
}

} // anonymous namespace

SearchDialog::SearchDialog(const QString& initialPath, QWidget* parent)
  : QDialog(parent)
  , m_pathEdit(nullptr)
  , m_browseButton(nullptr)
  , m_patternEdit(nullptr)
  , m_excludeEdit(nullptr)
  , m_subdirsCheck(nullptr)
  , m_searchButton(nullptr)
  , m_resultsTable(nullptr)
  , m_statusLabel(nullptr)
  , m_closeButton(nullptr)
  , m_worker(nullptr)
  , m_searching(false) {
  setupUi(initialPath);
}

SearchDialog::~SearchDialog() {
  stopSearch();
}

void SearchDialog::setupUi(const QString& initialPath) {
  setWindowTitle(tr("Search"));
  setModal(true);
  resize(820, 720);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  QFormLayout* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  // Start path + Browse
  QWidget* pathRow = new QWidget(this);
  QHBoxLayout* pathRowLayout = new QHBoxLayout(pathRow);
  pathRowLayout->setContentsMargins(0, 0, 0, 0);
  m_pathEdit = new QLineEdit(initialPath, this);
  m_pathEdit->setFocusPolicy(Qt::StrongFocus);
  m_browseButton = new QPushButton(this);
  m_browseButton->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
  m_browseButton->setToolTip(tr("Browse folder..."));
  m_browseButton->setFocusPolicy(Qt::StrongFocus);
  pathRowLayout->addWidget(m_pathEdit, 1);
  pathRowLayout->addWidget(m_browseButton);
  form->addRow(altBuddyLabel(tr("Start path:"), Qt::Key_P, m_pathEdit, this), pathRow);

  // Name pattern
  m_patternEdit = new QLineEdit(this);
  m_patternEdit->setPlaceholderText(tr("e.g. *.txt *.cpp (space-separated, empty for all)"));
  m_patternEdit->setFocusPolicy(Qt::StrongFocus);
  form->addRow(altBuddyLabel(tr("Name pattern:"), Qt::Key_N, m_patternEdit, this),
               m_patternEdit);

  // Exclude dirs
  m_excludeEdit = new QLineEdit(this);
  m_excludeEdit->setText(Settings::instance().searchExcludeDirs().join(QLatin1Char(' ')));
  m_excludeEdit->setPlaceholderText(tr("e.g. .* node_modules (space-separated)"));
  m_excludeEdit->setToolTip(
    tr("Directory names (glob) to skip when recursing. Default comes "
       "from Settings → Behavior. Changes here apply to this search only."));
  m_excludeEdit->setFocusPolicy(Qt::StrongFocus);
  form->addRow(altBuddyLabel(tr("Exclude dirs:"), Qt::Key_D, m_excludeEdit, this),
               m_excludeEdit);

  // Exclude files (file name patterns to skip). 例: *.log .DS_Store
  m_excludeFileEdit = new QLineEdit(this);
  m_excludeFileEdit->setPlaceholderText(tr("e.g. *.log .DS_Store (space-separated)"));
  m_excludeFileEdit->setToolTip(
    tr("File name patterns (glob) to skip from results. Applied after the "
       "name pattern filter."));
  m_excludeFileEdit->setFocusPolicy(Qt::StrongFocus);
  form->addRow(altBuddyLabel(tr("Exclude files:"), Qt::Key_E, m_excludeFileEdit, this),
               m_excludeFileEdit);

  // ── Alt ショートカットの方針 ───────────────────────────────
  // このダイアログでは「フォームの各行」「フィルタの ON/OFF チェック」
  // 「ダイアログのボタン」に Alt+key を割り当てる。フィルタ行の中の
  // 入力欄 (Min / Max / From / To / 単位 / Case sensitive) には付けない。
  // ON にした直後に Tab で辿れる位置にあり、字面の候補も尽きるため。
  // 割当: P 開始パス / N 名前 / D 除外ディレクトリ / E 除外ファイル /
  //       I ファイルのみ / R ディレクトリのみ / A 両方 /
  //       S サブディレクトリ / Z サイズ / M 更新日時 / T 内容 /
  //       F 検索 / C 閉じる
  //
  // 検索対象 (ファイル / ディレクトリ / 両方)。3 択なのでラジオボタンを横に並べる。
  // 同じ親の下に置くので autoExclusive が効き、←/→ で選択が移る。
  auto* targetRow = new QWidget(this);
  auto* targetRowLayout = new QHBoxLayout(targetRow);
  targetRowLayout->setContentsMargins(0, 0, 0, 0);
  m_targetFilesRadio = new QRadioButton(tr("Files only"), targetRow);
  m_targetDirsRadio  = new QRadioButton(tr("Directories only"), targetRow);
  m_targetBothRadio  = new QRadioButton(tr("Files and directories"), targetRow);
  applyAltShortcut(m_targetFilesRadio, Qt::Key_I);
  applyAltShortcut(m_targetDirsRadio,  Qt::Key_R);
  applyAltShortcut(m_targetBothRadio,  Qt::Key_A);
  m_targetFilesRadio->setChecked(true);
  for (QRadioButton* r : {m_targetFilesRadio, m_targetDirsRadio, m_targetBothRadio}) {
    r->setFocusPolicy(Qt::StrongFocus);
    targetRowLayout->addWidget(r);
  }
  targetRowLayout->addStretch(1);
  form->addRow(tr("Search for:"), targetRow);

  // Include subdirectories
  m_subdirsCheck = new QCheckBox(tr("Include subdirectories"), this);
  applyAltShortcut(m_subdirsCheck, Qt::Key_S);
  m_subdirsCheck->setChecked(true);
  m_subdirsCheck->setFocusPolicy(Qt::StrongFocus);
  form->addRow(QString(), m_subdirsCheck);

  mainLayout->addLayout(form);

  // ── 追加フィルタ (size / modified / content) ──
  QGroupBox* filterGroup = new QGroupBox(tr("Filters"), this);
  QGridLayout* filterGrid = new QGridLayout(filterGroup);
  filterGrid->setColumnStretch(5, 1);

  // Size
  m_sizeFilterCheck = new QCheckBox(tr("Size:"), this);
  applyAltShortcut(m_sizeFilterCheck, Qt::Key_Z);
  m_minSizeSpin = new QSpinBox(this);
  m_minSizeSpin->setRange(0, 1000000);
  m_minSizeSpin->setSpecialValueText(tr("Any"));
  m_minSizeUnit = new QComboBox(this);
  m_minSizeUnit->addItem(QStringLiteral("B"),   1LL);
  m_minSizeUnit->addItem(QStringLiteral("KB"),  1024LL);
  m_minSizeUnit->addItem(QStringLiteral("MB"),  1024LL * 1024);
  m_minSizeUnit->addItem(QStringLiteral("GB"),  1024LL * 1024 * 1024);
  m_minSizeUnit->setCurrentIndex(1); // KB
  m_maxSizeSpin = new QSpinBox(this);
  m_maxSizeSpin->setRange(0, 1000000);
  m_maxSizeSpin->setSpecialValueText(tr("Any"));
  m_maxSizeUnit = new QComboBox(this);
  m_maxSizeUnit->addItem(QStringLiteral("B"),   1LL);
  m_maxSizeUnit->addItem(QStringLiteral("KB"),  1024LL);
  m_maxSizeUnit->addItem(QStringLiteral("MB"),  1024LL * 1024);
  m_maxSizeUnit->addItem(QStringLiteral("GB"),  1024LL * 1024 * 1024);
  m_maxSizeUnit->setCurrentIndex(1);
  auto* minSizeLabel = new QLabel(tr("Min:"), this);
  auto* maxSizeLabel = new QLabel(tr("Max:"), this);
  filterGrid->addWidget(m_sizeFilterCheck, 0, 0);
  filterGrid->addWidget(minSizeLabel, 0, 1);
  filterGrid->addWidget(m_minSizeSpin, 0, 2);
  filterGrid->addWidget(m_minSizeUnit, 0, 3);
  filterGrid->addWidget(maxSizeLabel, 0, 4);
  QHBoxLayout* maxRow = new QHBoxLayout();
  maxRow->setContentsMargins(0, 0, 0, 0);
  maxRow->addWidget(m_maxSizeSpin);
  maxRow->addWidget(m_maxSizeUnit);
  maxRow->addStretch(1);
  filterGrid->addLayout(maxRow, 0, 5);

  // Modified date
  m_dateFilterCheck = new QCheckBox(tr("Modified:"), this);
  applyAltShortcut(m_dateFilterCheck, Qt::Key_M);
  m_dateFromEdit = new QDateTimeEdit(QDateTime(QDate::currentDate().addYears(-1), QTime(0, 0)), this);
  m_dateFromEdit->setCalendarPopup(true);
  m_dateFromEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
  m_dateToEdit = new QDateTimeEdit(QDateTime(QDate::currentDate(), QTime(23, 59)), this);
  m_dateToEdit->setCalendarPopup(true);
  m_dateToEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
  filterGrid->addWidget(m_dateFilterCheck, 1, 0);
  filterGrid->addWidget(new QLabel(tr("From:"), this), 1, 1);
  filterGrid->addWidget(m_dateFromEdit, 1, 2, 1, 2);
  filterGrid->addWidget(new QLabel(tr("To:"), this), 1, 4);
  filterGrid->addWidget(m_dateToEdit, 1, 5);

  // Content text
  m_contentFilterCheck = new QCheckBox(tr("Content:"), this);
  applyAltShortcut(m_contentFilterCheck, Qt::Key_T);
  m_contentEdit = new QLineEdit(this);
  m_contentEdit->setPlaceholderText(tr("Text to find inside files"));
  m_contentCsCheck = new QCheckBox(tr("Case sensitive"), this);
  filterGrid->addWidget(m_contentFilterCheck, 2, 0);
  filterGrid->addWidget(m_contentEdit, 2, 1, 1, 4);
  filterGrid->addWidget(m_contentCsCheck, 2, 5);

  // Master チェックで子コントロールを enable/disable。
  // 注: initializer_list はバッキング配列の寿命が呼び出し式の終わりまでなので
  //     ラムダで value-capture すると dangling になる。一度 QList にコピー
  //     してから capture する。
  auto wireMaster = [](QCheckBox* master, std::initializer_list<QWidget*> initChildren) {
    QList<QWidget*> children(initChildren.begin(), initChildren.end());
    auto apply = [master, children]() {
      for (QWidget* w : children) w->setEnabled(master->isChecked());
    };
    QObject::connect(master, &QCheckBox::toggled, master, apply);
    apply();
  };
  // 更新日時はディレクトリにも意味があるので、対象種別によらず従来どおり。
  wireMaster(m_dateFilterCheck,
             {m_dateFromEdit, m_dateToEdit});

  // サイズ / 内容は「ディレクトリのみ」で行ごと無効化したいので、
  // マスターチェックだけでなく検索対象も見る updateFilterAvailability に任せる。
  m_sizeFilterWidgets = {minSizeLabel, m_minSizeSpin, m_minSizeUnit,
                         maxSizeLabel, m_maxSizeSpin, m_maxSizeUnit};
  m_contentFilterWidgets = {m_contentEdit, m_contentCsCheck};
  connect(m_sizeFilterCheck,    &QCheckBox::toggled,
          this, &SearchDialog::updateFilterAvailability);
  connect(m_contentFilterCheck, &QCheckBox::toggled,
          this, &SearchDialog::updateFilterAvailability);
  connect(m_targetDirsRadio, &QRadioButton::toggled,
          this, &SearchDialog::updateFilterAvailability);
  updateFilterAvailability();

  // macOS のキーボードナビゲーション設定に依存させずに Tab で全部辿れるよう
  // 拡張フィルタの全コントロールに StrongFocus を明示。
  for (QWidget* w : QList<QWidget*>{
         m_sizeFilterCheck, m_minSizeSpin, m_minSizeUnit,
         m_maxSizeSpin, m_maxSizeUnit,
         m_dateFilterCheck, m_dateFromEdit, m_dateToEdit,
         m_contentFilterCheck, m_contentEdit, m_contentCsCheck}) {
    w->setFocusPolicy(Qt::StrongFocus);
  }

  mainLayout->addWidget(filterGroup);

  // Search / Stop button
  QHBoxLayout* searchRow = new QHBoxLayout();
  m_searchButton = new QPushButton(tr("Search"), this);
  applyAltShortcut(m_searchButton, Qt::Key_F);  // Find
  m_searchButton->setDefault(true);
  searchRow->addStretch(1);
  searchRow->addWidget(m_searchButton);
  mainLayout->addLayout(searchRow);

  // Results table
  m_resultsTable = new QTableWidget(this);
  m_resultsTable->setWordWrap(false);  // 折り返さず省略 (…) で 1 行表示
  m_resultsTable->setColumnCount(4);
  m_resultsTable->setHorizontalHeaderLabels(
    {tr("Name"), tr("Path"), tr("Size"), tr("Modified")});
  m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_resultsTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_resultsTable->verticalHeader()->setVisible(false);
  m_resultsTable->horizontalHeader()->setStretchLastSection(false);
  m_resultsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  // Path 以外の列を少し広めに (Name は長めに、Size/Modified は読みやすい幅に)
  m_resultsTable->setColumnWidth(0, 220);  // Name
  m_resultsTable->setColumnWidth(2, 110);  // Size
  m_resultsTable->setColumnWidth(3, 160);  // Modified
  m_resultsTable->setShowGrid(false);
  m_resultsTable->setTabKeyNavigation(false);  // Tab はボタンへ抜ける
  m_resultsTable->setFocusPolicy(Qt::StrongFocus);
  // 検索開始時に setCurrentCell で先頭行が「選択中」になるが、フォーカスが
  // 入力欄に居る状態で青反転表示だと「テーブルがアクティブ」に見えてしまう。
  // 共通ヘルパで非アクティブ時はグレー + 通常テキスト色にする。
  m_resultsTable->setStyleSheet(inactiveSelectionStyleSheet());
  mainLayout->addWidget(m_resultsTable, 1);

  m_statusLabel = new QLabel(tr("Ready."), this);
  mainLayout->addWidget(m_statusLabel);

  // Bottom buttons
  QHBoxLayout* btnLayout = new QHBoxLayout();
  btnLayout->addStretch(1);
  m_closeButton = new QPushButton(tr("Close"), this);
  applyAltShortcut(m_closeButton, Qt::Key_C);
  btnLayout->addWidget(m_closeButton);
  mainLayout->addLayout(btnLayout);

  // Signals
  connect(m_browseButton,  &QPushButton::clicked, this, &SearchDialog::onBrowse);
  connect(m_searchButton,  &QPushButton::clicked, this, &SearchDialog::onSearchOrStop);
  connect(m_closeButton,   &QPushButton::clicked, this, &QDialog::reject);
  // ダブルクリックで結果行のファイルへ移動
  connect(m_resultsTable,  &QTableWidget::itemDoubleClicked,
          this, [this](QTableWidgetItem*) { onGoTo(); });
  // Enter は QDialog の default button（Search）が先に拾ってしまうので、
  // 結果テーブル上のキー入力を eventFilter で先取りする。
  m_resultsTable->installEventFilter(this);

  // Tab 順: 視覚的な並び (上から下) と一致させる。
  //   path → browse → pattern → exclude → subdirs
  //   → size filter (master + min/max + units)
  //   → date filter (master + from/to)
  //   → content filter (master + text + case sensitive)
  //   → Search → table → Close
  setTabOrder(m_pathEdit,           m_browseButton);
  setTabOrder(m_browseButton,       m_patternEdit);
  setTabOrder(m_patternEdit,        m_excludeEdit);
  setTabOrder(m_excludeEdit,        m_excludeFileEdit);
  setTabOrder(m_excludeFileEdit,    m_targetFilesRadio);
  setTabOrder(m_targetFilesRadio,   m_targetDirsRadio);
  setTabOrder(m_targetDirsRadio,    m_targetBothRadio);
  setTabOrder(m_targetBothRadio,    m_subdirsCheck);
  setTabOrder(m_subdirsCheck,       m_sizeFilterCheck);
  setTabOrder(m_sizeFilterCheck,    m_minSizeSpin);
  setTabOrder(m_minSizeSpin,        m_minSizeUnit);
  setTabOrder(m_minSizeUnit,        m_maxSizeSpin);
  setTabOrder(m_maxSizeSpin,        m_maxSizeUnit);
  setTabOrder(m_maxSizeUnit,        m_dateFilterCheck);
  setTabOrder(m_dateFilterCheck,    m_dateFromEdit);
  setTabOrder(m_dateFromEdit,       m_dateToEdit);
  setTabOrder(m_dateToEdit,         m_contentFilterCheck);
  setTabOrder(m_contentFilterCheck, m_contentEdit);
  setTabOrder(m_contentEdit,        m_contentCsCheck);
  setTabOrder(m_contentCsCheck,     m_searchButton);
  setTabOrder(m_searchButton,       m_resultsTable);
  setTabOrder(m_resultsTable,       m_closeButton);

  m_patternEdit->setFocus();
}

bool SearchDialog::eventFilter(QObject* obj, QEvent* event) {
  if (obj == m_resultsTable && event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
      if (m_resultsTable->currentRow() >= 0) {
        onGoTo();
        return true;
      }
    }
  }
  return QDialog::eventFilter(obj, event);
}

void SearchDialog::keyPressEvent(QKeyEvent* event) {
  if (event->modifiers() & Qt::AltModifier) {
    switch (event->key()) {
      case Qt::Key_S:
        m_subdirsCheck->setChecked(!m_subdirsCheck->isChecked());
        event->accept();
        return;
      default: break;
    }
  }
  QDialog::keyPressEvent(event);
}

void SearchDialog::onBrowse() {
  const QString start = m_pathEdit->text().trimmed().isEmpty()
                          ? QString()
                          : m_pathEdit->text().trimmed();
  const QString selected = QFileDialog::getExistingDirectory(
    this, tr("Select Start Directory"), start,
    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  if (!selected.isEmpty()) {
    m_pathEdit->setText(selected);
  }
}

void SearchDialog::onSearchOrStop() {
  if (m_searching) {
    stopSearch();
  } else {
    startSearch();
  }
}

void SearchDialog::startSearch() {
  const QString rootPath = m_pathEdit->text().trimmed();
  if (rootPath.isEmpty()) return;

  // パターン: 空白で分割
  auto splitPatterns = [](const QString& text) {
    return text.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
  };
  const QStringList patterns            = splitPatterns(m_patternEdit->text());
  const QStringList excludeDirPatterns  = splitPatterns(m_excludeEdit->text());
  const QStringList excludeFilePatterns = splitPatterns(m_excludeFileEdit->text());
  const bool includeSubdirs = m_subdirsCheck->isChecked();
  const SearchTarget target = currentTarget();

  // 結果テーブルをクリア
  m_resultsTable->setRowCount(0);
  m_statusLabel->setText(tr("Searching..."));

  // 拡張フィルタを組み立てる
  // 無効化されているフィルタは、チェックが残っていても組み立てない
  // (「ディレクトリのみ」に切り替える前のチェックが効いてしまうのを防ぐ)。
  SearchFilter filter;
  if (m_sizeFilterCheck->isEnabled() && m_sizeFilterCheck->isChecked()) {
    filter.sizeEnabled = true;
    filter.minSize = static_cast<qint64>(m_minSizeSpin->value())
                       * m_minSizeUnit->currentData().toLongLong();
    filter.maxSize = static_cast<qint64>(m_maxSizeSpin->value())
                       * m_maxSizeUnit->currentData().toLongLong();
  }
  if (m_dateFilterCheck->isChecked()) {
    filter.modifiedEnabled = true;
    filter.modifiedFrom = m_dateFromEdit->dateTime();
    filter.modifiedTo   = m_dateToEdit->dateTime();
  }
  if (m_contentFilterCheck->isEnabled() && m_contentFilterCheck->isChecked()) {
    const QString text = m_contentEdit->text();
    if (!text.isEmpty()) {
      filter.contentEnabled       = true;
      filter.contentBytes         = text.toUtf8();
      filter.contentCaseSensitive = m_contentCsCheck->isChecked();
    }
  }

  m_worker = new SearchWorker(rootPath, patterns, excludeDirPatterns,
                              excludeFilePatterns, includeSubdirs,
                              filter, target, this);
  connect(m_worker, &SearchWorker::resultFound, this, &SearchDialog::onResultFound);
  connect(m_worker, &WorkerBase::finished,      this, &SearchDialog::onFinished);
  m_searching = true;
  m_searchButton->setText(tr("Stop"));
  m_worker->start();
}

void SearchDialog::stopSearch() {
  if (m_worker) {
    m_worker->requestCancel();
    m_worker->wait();
    m_worker->deleteLater();
    m_worker = nullptr;
  }
  m_searching = false;
  if (m_searchButton) m_searchButton->setText(tr("Search"));
}

void SearchDialog::onResultFound(const QString& path) {
  appendResultRow(path);
  m_statusLabel->setText(tr("Searching... %1 found").arg(m_resultsTable->rowCount()));
}

void SearchDialog::onFinished(bool /*success*/) {
  m_searching = false;
  if (m_searchButton) m_searchButton->setText(tr("Search"));
  m_statusLabel->setText(tr("Done. %1 found.").arg(m_resultsTable->rowCount()));
  if (m_worker) {
    m_worker->deleteLater();
    m_worker = nullptr;
  }
}

void SearchDialog::appendResultRow(const QString& path) {
  const QFileInfo info(path);
  const int row = m_resultsTable->rowCount();
  m_resultsTable->insertRow(row);

  auto* nameItem = new QTableWidgetItem(info.fileName());
  auto* pathItem = new QTableWidgetItem(info.absolutePath());
  auto* sizeItem = new QTableWidgetItem(
    info.isDir() ? QStringLiteral("<DIR>") : humanSize(info.size()));
  auto* timeItem = new QTableWidgetItem(
    info.lastModified().toString(QStringLiteral("yyyy/MM/dd HH:mm")));

  // パスを隠しデータとしてフルパスを保持
  nameItem->setData(Qt::UserRole, info.absoluteFilePath());

  m_resultsTable->setItem(row, 0, nameItem);
  m_resultsTable->setItem(row, 1, pathItem);
  m_resultsTable->setItem(row, 2, sizeItem);
  m_resultsTable->setItem(row, 3, timeItem);
  if (row == 0) {
    m_resultsTable->setCurrentCell(0, 0);
  }
}

SearchTarget SearchDialog::currentTarget() const {
  if (m_targetDirsRadio && m_targetDirsRadio->isChecked()) {
    return SearchTarget::Directories;
  }
  if (m_targetBothRadio && m_targetBothRadio->isChecked()) {
    return SearchTarget::Both;
  }
  return SearchTarget::Files;
}

void SearchDialog::updateFilterAvailability() {
  // サイズと内容はディレクトリに意味が無い。「ディレクトリのみ」のときは
  // マスターチェックごと無効化して、設定できないことを見た目で示す。
  const bool available = (currentTarget() != SearchTarget::Directories);

  m_sizeFilterCheck->setEnabled(available);
  const bool sizeOn = available && m_sizeFilterCheck->isChecked();
  for (QWidget* w : m_sizeFilterWidgets) w->setEnabled(sizeOn);

  m_contentFilterCheck->setEnabled(available);
  const bool contentOn = available && m_contentFilterCheck->isChecked();
  for (QWidget* w : m_contentFilterWidgets) w->setEnabled(contentOn);
}

void SearchDialog::onGoTo() {
  const int row = m_resultsTable->currentRow();
  if (row < 0) return;
  const QTableWidgetItem* item = m_resultsTable->item(row, 0);
  if (!item) return;
  m_selectedPath = item->data(Qt::UserRole).toString();
  accept();
}

} // namespace Farman
