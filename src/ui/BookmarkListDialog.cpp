#include "BookmarkListDialog.h"
#include "BookmarkEditDialog.h"
#include "core/BookmarkManager.h"
#include "settings/Settings.h"
#include "utils/Dialogs.h"
#include "utils/EnterClickFilter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QInputDialog>
#include <QLineEdit>
#include <QAbstractItemView>
#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <QFont>
#include <QBrush>
#include <QColor>

namespace Farman {

namespace {

// 動的検出: /Volumes/* と一部の明示パス。存在するもののみ返す。
// CloudStorage 以下は見ない。
QList<QPair<QString, QString>> buildDetectedLocations() {
  QList<QPair<QString, QString>> list;

  auto addIfExists = [&](const QString& name, const QString& path) {
    if (!path.isEmpty() && QDir(path).exists()) {
      list.append({name, path});
    }
  };

#ifdef Q_OS_WIN
  // Windows: ローカル / マップ済みネットワークドライブを列挙する。
  // QDir::drives() は接続済みのドライブ (C:/, D:/, マップ済みの Z:/ など) を
  // 返す。未マップの UNC パス (\\server\share) は含まれないので、それは手動
  // 登録 (b キー / パス入力) になる。表示名はボリュームラベルがあれば
  // 「ラベル (C:)」、無ければドライブレターのみとする。
  for (const QFileInfo& drive : QDir::drives()) {
    const QString root = drive.absoluteFilePath();  // 例: "C:/"
    if (root.isEmpty() || !QDir(root).exists()) continue;
    const QString letter = root.left(2);            // 例: "C:"
    const QString label  = QStorageInfo(root).displayName().trimmed();
    const QString name   = (label.isEmpty() || label == root || label == letter)
                             ? letter
                             : QStringLiteral("%1 (%2)").arg(label, letter);
    list.append({name, root});
  }
#endif

  // /Volumes/* (外部ディスク、ネットワークマウント、一部クラウド)
  QDir volumes(QStringLiteral("/Volumes"));
  if (volumes.exists()) {
    const QFileInfoList infos = volumes.entryInfoList(
      QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : infos) {
      if (fi.isSymLink()) continue;  // macOS のルートへの別名を除外
      addIfExists(fi.fileName(), fi.absoluteFilePath());
    }
  }

  // レガシー / 非標準位置にあるクラウドマウント
  const QString home = QDir::homePath();
  addIfExists(QObject::tr("iCloud Drive"),
              home + QStringLiteral("/Library/Mobile Documents/com~apple~CloudDocs"));
  addIfExists(QStringLiteral("Dropbox"),      home + QStringLiteral("/Dropbox"));
  addIfExists(QStringLiteral("Google Drive"), home + QStringLiteral("/Google Drive"));

  return list;
}

} // anonymous namespace

BookmarkListDialog::BookmarkListDialog(QWidget* parent)
  : QDialog(parent)
  , m_table(nullptr)
  , m_goButton(nullptr)
  , m_editButton(nullptr)
  , m_deleteButton(nullptr)
  , m_upButton(nullptr)
  , m_downButton(nullptr) {

  setWindowTitle(tr("Bookmarks"));
  resize(600, 520);
  setupUi();
  rebuildTable();

  connect(&BookmarkManager::instance(), &BookmarkManager::bookmarksChanged,
          this, &BookmarkListDialog::rebuildTable);
}

void BookmarkListDialog::setupUi() {
  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  m_table = new QTableWidget(this);
  m_table->setStyleSheet(inactiveSelectionStyleSheet());
  m_table->setColumnCount(2);
  m_table->setHorizontalHeaderLabels({tr("Name"), tr("Path")});
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setAlternatingRowColors(false);
  m_table->verticalHeader()->setVisible(false);
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
  m_table->setShowGrid(false);
  // Tab はボタンへ抜けさせる。行選択は上下キーで行う。
  m_table->setTabKeyNavigation(false);

  connect(m_table, &QTableWidget::itemDoubleClicked,
          this, [this](QTableWidgetItem*) { onGo(); });
  connect(m_table, &QTableWidget::currentCellChanged,
          this, [this](int, int, int, int) { onSelectionChanged(); });
  mainLayout->addWidget(m_table, 1);

  QHBoxLayout* btnLayout = new QHBoxLayout();
  m_goButton     = new QPushButton(tr("Go"),     this);
  m_editButton   = new QPushButton(tr("Edit"),   this);
  m_deleteButton = new QPushButton(tr("Delete"), this);
  m_upButton     = new QPushButton(tr("↑"),      this);
  m_downButton   = new QPushButton(tr("↓"),      this);
  QPushButton* closeButton = new QPushButton(tr("Close"), this);

  applyAltShortcut(m_goButton,     Qt::Key_G);
  // macOS では Option+E が dead key（acute accent）に予約されているため T を使う
  applyAltShortcut(m_editButton,   Qt::Key_T);
  applyAltShortcut(m_deleteButton, Qt::Key_D);
  applyAltShortcut(m_upButton,     Qt::Key_Up);
  applyAltShortcut(m_downButton,   Qt::Key_Down);
  applyAltShortcut(closeButton,    Qt::Key_C);

  m_goButton->setDefault(true);
  m_upButton->setToolTip(tr("Move selected bookmark up"));
  m_downButton->setToolTip(tr("Move selected bookmark down"));

  connect(m_goButton,     &QPushButton::clicked, this, &BookmarkListDialog::onGo);
  connect(m_editButton,   &QPushButton::clicked, this, &BookmarkListDialog::onEdit);
  connect(m_deleteButton, &QPushButton::clicked, this, &BookmarkListDialog::onDelete);
  connect(m_upButton,     &QPushButton::clicked, this, &BookmarkListDialog::onMoveUp);
  connect(m_downButton,   &QPushButton::clicked, this, &BookmarkListDialog::onMoveDown);
  connect(closeButton,    &QPushButton::clicked, this, &QDialog::reject);

  btnLayout->addWidget(m_goButton);
  btnLayout->addWidget(m_editButton);
  btnLayout->addWidget(m_deleteButton);
  btnLayout->addWidget(m_upButton);
  btnLayout->addWidget(m_downButton);
  btnLayout->addStretch(1);
  btnLayout->addWidget(closeButton);

  mainLayout->addLayout(btnLayout);

  // Tab 順: table → Edit → Delete → ↑ → ↓ → Close → Go。
  // accept を呼ぶ Go を最後に配置して誤操作を防ぐ。
  m_table->setFocusPolicy(Qt::StrongFocus);
  setTabOrder(m_table,        m_editButton);
  setTabOrder(m_editButton,   m_deleteButton);
  setTabOrder(m_deleteButton, m_upButton);
  setTabOrder(m_upButton,     m_downButton);
  setTabOrder(m_downButton,   closeButton);
  setTabOrder(closeButton,    m_goButton);

  onSelectionChanged();
}

int BookmarkListDialog::currentRow() const {
  return m_table ? m_table->currentRow() : -1;
}

BookmarkListDialog::RowInfo BookmarkListDialog::currentRowInfo() const {
  const int row = currentRow();
  RowInfo info;
  if (row < 0 || row >= m_rowInfos.size()) return info;
  return m_rowInfos[row];
}

void BookmarkListDialog::rebuildTable() {
  if (!m_table) return;

  // 現在選択中のパスを保存（再構築後に同じ行を選択し直すため）
  const int prevRow = m_table->currentRow();
  QString prevPath;
  if (prevRow >= 0 && prevRow < m_rowInfos.size()) {
    const RowInfo& prev = m_rowInfos[prevRow];
    if (prev.type == RowInfo::Type::Default || prev.type == RowInfo::Type::User) {
      const QList<Bookmark> bs = BookmarkManager::instance().bookmarks();
      if (prev.sourceIndex >= 0 && prev.sourceIndex < bs.size()) {
        prevPath = bs[prev.sourceIndex].path;
      }
    } else if (prev.type == RowInfo::Type::Detected) {
      if (prev.sourceIndex >= 0 && prev.sourceIndex < m_detectedPaths.size()) {
        prevPath = m_detectedPaths[prev.sourceIndex];
      }
    }
  }

  // ソースデータ
  const QList<Bookmark> bookmarks = BookmarkManager::instance().bookmarks();
  const QList<QPair<QString, QString>> detected = buildDetectedLocations();

  // BookmarkManager のリストから isDefault で分類
  QList<int> defaultIdxs;
  QList<int> userIdxs;
  for (int i = 0; i < bookmarks.size(); ++i) {
    if (bookmarks[i].isDefault) defaultIdxs.append(i);
    else                         userIdxs.append(i);
  }

  // 行数を算出: default + (separator 1 + detected) + user
  //            （detected が 0 件でもセパレータは出す：位置関係を示すため）
  m_rowInfos.clear();
  m_detectedPaths.clear();

  // Default 行
  for (int i : defaultIdxs) {
    RowInfo ri;
    ri.type = RowInfo::Type::Default;
    ri.sourceIndex = i;
    m_rowInfos.append(ri);
  }
  // Detected 行群（見出しなしでそのまま続ける。視覚的な区別は文字色のみ）
  for (int i = 0; i < detected.size(); ++i) {
    m_detectedPaths.append(detected[i].second);
    RowInfo ri;
    ri.type = RowInfo::Type::Detected;
    ri.sourceIndex = i;
    m_rowInfos.append(ri);
  }
  // User 行
  for (int i : userIdxs) {
    RowInfo ri;
    ri.type = RowInfo::Type::User;
    ri.sourceIndex = i;
    m_rowInfos.append(ri);
  }

  // テーブル描画
  m_table->clearSpans();
  m_table->setRowCount(m_rowInfos.size());

  const QBrush separatorBg(QColor(230, 230, 240));
  QFont boldFont = m_table->font();
  boldFont.setBold(true);
  const QBrush detectedFg(QColor(80, 80, 100));

  for (int r = 0; r < m_rowInfos.size(); ++r) {
    const RowInfo& ri = m_rowInfos[r];
    if (ri.type == RowInfo::Type::Separator) {
      auto* item = new QTableWidgetItem(tr("Detected locations"));
      item->setFont(boldFont);
      item->setBackground(separatorBg);
      item->setFlags(Qt::ItemIsEnabled);  // 選択不可
      m_table->setItem(r, 0, item);
      m_table->setSpan(r, 0, 1, 2);
      continue;
    }

    QString name, path;
    if (ri.type == RowInfo::Type::Default || ri.type == RowInfo::Type::User) {
      if (ri.sourceIndex >= 0 && ri.sourceIndex < bookmarks.size()) {
        name = bookmarks[ri.sourceIndex].name;
        path = bookmarks[ri.sourceIndex].path;
      }
    } else if (ri.type == RowInfo::Type::Detected) {
      if (ri.sourceIndex >= 0 && ri.sourceIndex < detected.size()) {
        name = detected[ri.sourceIndex].first;
        path = detected[ri.sourceIndex].second;
      }
    }

    auto* nameItem = new QTableWidgetItem(name);
    auto* pathItem = new QTableWidgetItem(path);
    // Default と Detected は「編集不可」という共通性があるので文字色も揃える
    if (ri.type == RowInfo::Type::Default || ri.type == RowInfo::Type::Detected) {
      nameItem->setForeground(detectedFg);
      pathItem->setForeground(detectedFg);
    }
    m_table->setItem(r, 0, nameItem);
    m_table->setItem(r, 1, pathItem);
  }

  // Name 列は初回のみ自動フィット
  static bool resized = false;
  if (!resized) {
    m_table->resizeColumnToContents(0);
    resized = true;
  }

  // 以前選択していたパスに再選択（なければ最初の選択可能行）
  int newRow = -1;
  if (!prevPath.isEmpty()) {
    for (int r = 0; r < m_rowInfos.size(); ++r) {
      const RowInfo& ri = m_rowInfos[r];
      if (ri.type == RowInfo::Type::Separator) continue;
      const QTableWidgetItem* item = m_table->item(r, 1);
      if (item && item->text() == prevPath) { newRow = r; break; }
    }
  }
  if (newRow < 0) {
    for (int r = 0; r < m_rowInfos.size(); ++r) {
      if (m_rowInfos[r].type != RowInfo::Type::Separator) { newRow = r; break; }
    }
  }
  if (newRow >= 0) m_table->setCurrentCell(newRow, 0);

  onSelectionChanged();
}

void BookmarkListDialog::onSelectionChanged() {
  const RowInfo info = currentRowInfo();

  const bool isDefault  = info.type == RowInfo::Type::Default;
  const bool isUser     = info.type == RowInfo::Type::User;
  const bool isDetected = info.type == RowInfo::Type::Detected;
  const bool isAnyBookmark = isDefault || isUser;
  const bool canGo = isAnyBookmark || isDetected;

  // Rename は User のみ、Delete も User のみ、Move は同セクション内のみ
  if (m_goButton)     m_goButton->setEnabled(canGo);
  if (m_editButton)   m_editButton->setEnabled(isUser);
  if (m_deleteButton) m_deleteButton->setEnabled(isUser);

  bool canUp = false, canDown = false;
  if (isUser) {
    // User 行同士の並び替えのみ許可（Default/Detected は不動）
    const int r = currentRow();
    if (r > 0 && m_rowInfos[r - 1].type == RowInfo::Type::User) canUp = true;
    if (r >= 0 && r + 1 < m_rowInfos.size()
        && m_rowInfos[r + 1].type == RowInfo::Type::User) canDown = true;
  }
  if (m_upButton)   m_upButton->setEnabled(canUp);
  if (m_downButton) m_downButton->setEnabled(canDown);
}

void BookmarkListDialog::onGo() {
  const RowInfo info = currentRowInfo();
  QString path;
  if (info.type == RowInfo::Type::Default || info.type == RowInfo::Type::User) {
    const QList<Bookmark> list = BookmarkManager::instance().bookmarks();
    if (info.sourceIndex >= 0 && info.sourceIndex < list.size()) {
      path = list[info.sourceIndex].path;
    }
  } else if (info.type == RowInfo::Type::Detected) {
    if (info.sourceIndex >= 0 && info.sourceIndex < m_detectedPaths.size()) {
      path = m_detectedPaths[info.sourceIndex];
    }
  }
  if (path.isEmpty()) return;
  m_selectedPath = path;
  accept();
}

void BookmarkListDialog::onEdit() {
  const RowInfo info = currentRowInfo();
  if (info.type != RowInfo::Type::User) return;  // Default/Detected は編集不可
  const QList<Bookmark> list = BookmarkManager::instance().bookmarks();
  if (info.sourceIndex < 0 || info.sourceIndex >= list.size()) return;

  const Bookmark& b = list[info.sourceIndex];
  BookmarkEditDialog dlg(b.name, b.path, this);
  if (dlg.exec() != QDialog::Accepted) return;
  BookmarkManager::instance().edit(info.sourceIndex, dlg.name(), dlg.path());
}

void BookmarkListDialog::onDelete() {
  const RowInfo info = currentRowInfo();
  if (info.type != RowInfo::Type::User) return;  // Default も Detected も削除不可
  const QList<Bookmark> list = BookmarkManager::instance().bookmarks();
  if (info.sourceIndex < 0 || info.sourceIndex >= list.size()) return;

  const Bookmark& b = list[info.sourceIndex];
  const QString label = b.name.isEmpty() ? b.path : b.name;
  if (!confirm(this, tr("Delete Bookmark"),
               tr("Delete bookmark '%1'?").arg(label))) {
    return;
  }

  BookmarkManager::instance().removeAt(info.sourceIndex);
}

void BookmarkListDialog::onMoveUp() {
  const RowInfo info = currentRowInfo();
  if (info.type != RowInfo::Type::User) return;  // User 同士のみ
  const int r = currentRow();
  if (r <= 0) return;
  if (m_rowInfos[r - 1].type != RowInfo::Type::User) return;
  const int targetSource = m_rowInfos[r - 1].sourceIndex;
  BookmarkManager::instance().move(info.sourceIndex, targetSource);
}

void BookmarkListDialog::onMoveDown() {
  const RowInfo info = currentRowInfo();
  if (info.type != RowInfo::Type::User) return;
  const int r = currentRow();
  if (r < 0 || r + 1 >= m_rowInfos.size()) return;
  if (m_rowInfos[r + 1].type != RowInfo::Type::User) return;
  const int targetSource = m_rowInfos[r + 1].sourceIndex;
  BookmarkManager::instance().move(info.sourceIndex, targetSource);
}

} // namespace Farman
