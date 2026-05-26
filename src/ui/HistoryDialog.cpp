#include "HistoryDialog.h"
#include "utils/Dialogs.h"
#include "utils/EnterClickFilter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QAbstractItemView>
#include <QLabel>

namespace Farman {

HistoryDialog::HistoryDialog(const QStringList& entries, QWidget* parent)
  : QDialog(parent)
  , m_table(nullptr)
  , m_goButton(nullptr) {

  setWindowTitle(tr("History"));
  resize(560, 320);
  setupUi(entries);
}

void HistoryDialog::setupUi(const QStringList& entries) {
  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  if (entries.isEmpty()) {
    QLabel* emptyLabel = new QLabel(tr("No history yet."), this);
    emptyLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(emptyLabel, 1);
  } else {
    m_table = new QTableWidget(this);
    m_table->setStyleSheet(inactiveSelectionStyleSheet());
    m_table->setColumnCount(1);
    m_table->setHorizontalHeaderLabels({tr("Path")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setShowGrid(false);
    // Tab はボタンへ抜けさせる。行選択は上下キーで行う。
    m_table->setTabKeyNavigation(false);

    m_table->setRowCount(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
      m_table->setItem(i, 0, new QTableWidgetItem(entries[i]));
    }
    if (!entries.isEmpty()) m_table->setCurrentCell(0, 0);

    connect(m_table, &QTableWidget::itemDoubleClicked,
            this, [this](QTableWidgetItem*) { onGo(); });
    connect(m_table, &QTableWidget::currentCellChanged,
            this, [this](int, int, int, int) { onSelectionChanged(); });
    mainLayout->addWidget(m_table, 1);
  }

  QHBoxLayout* btnLayout = new QHBoxLayout();
  m_goButton = new QPushButton(tr("Go"), this);
  QPushButton* closeButton = new QPushButton(tr("Close"), this);

  applyAltShortcut(m_goButton,  Qt::Key_G);
  applyAltShortcut(closeButton, Qt::Key_C);
  m_goButton->setDefault(true);

  connect(m_goButton,  &QPushButton::clicked, this, &HistoryDialog::onGo);
  connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

  btnLayout->addStretch(1);
  btnLayout->addWidget(m_goButton);
  btnLayout->addWidget(closeButton);
  mainLayout->addLayout(btnLayout);

  // Tab 順: table（あれば）→ Close → Go。
  // accept を呼ぶ Go を最後に配置して誤操作を防ぐ。
  if (m_table) {
    m_table->setFocusPolicy(Qt::StrongFocus);
    setTabOrder(m_table, closeButton);
  }
  setTabOrder(closeButton, m_goButton);

  onSelectionChanged();
}

void HistoryDialog::onSelectionChanged() {
  const bool hasRow = m_table && m_table->currentRow() >= 0;
  if (m_goButton) m_goButton->setEnabled(hasRow);
}

void HistoryDialog::onGo() {
  if (!m_table) return;
  const int row = m_table->currentRow();
  if (row < 0) return;
  const QTableWidgetItem* item = m_table->item(row, 0);
  if (!item) return;
  m_selectedPath = item->text();
  accept();
}

} // namespace Farman
