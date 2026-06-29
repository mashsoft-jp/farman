#include "PropertiesDialog.h"
#include "core/workers/PropertiesWorker.h"
#include "core/workers/WorkerBase.h"
#include "utils/Dialogs.h"
#include <QBoxLayout>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QDateTimeEdit>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Farman {

namespace {

QString formatSize(qint64 bytes) {
  const QString num = QLocale(QLocale::English).toString(bytes);
  const QString human = QLocale(QLocale::English).formattedDataSize(bytes);
  if (bytes < 1024) return QStringLiteral("%1 B").arg(num);
  return QStringLiteral("%1 B (%2)").arg(num, human);
}

QString formatDate(const QDateTime& t) {
  if (!t.isValid()) return QStringLiteral("—");
  return t.toString(QStringLiteral("yyyy/MM/dd HH:mm:ss"));
}

// 更新日時 (mtime) を変更する。atime は据え置く。ファイル / ディレクトリ両対応。
bool setModificationTime(const QString& path, const QDateTime& dt) {
#ifdef Q_OS_WIN
  QFile f(path);
  if (!f.open(QIODevice::ReadWrite)) return false;
  const bool ok = f.setFileTime(dt, QFileDevice::FileModificationTime);
  f.close();
  return ok;
#else
  struct timespec times[2];
  times[0].tv_sec = 0; times[0].tv_nsec = UTIME_OMIT;            // atime 据え置き
  times[1].tv_sec = dt.toSecsSinceEpoch(); times[1].tv_nsec = 0;  // mtime を設定
  return ::utimensat(AT_FDCWD, path.toLocal8Bit().constData(), times, 0) == 0;
#endif
}

#ifdef Q_OS_WIN
DWORD windowsAttributes(const QString& path) {
  return GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
}
#else
// 所有者 / グループを変更する (chown)。名前で指定し、見つからなければ数値 uid/gid
// として解釈する。空欄は「変更しない」(-1)。通常 root 権限が要る。
bool changeOwnerGroup(const QString& path, const QString& owner, const QString& group) {
  uid_t uid = static_cast<uid_t>(-1);
  gid_t gid = static_cast<gid_t>(-1);
  if (!owner.isEmpty()) {
    if (struct passwd* pw = ::getpwnam(owner.toLocal8Bit().constData())) {
      uid = pw->pw_uid;
    } else {
      bool ok = false; uid = owner.toUInt(&ok); if (!ok) return false;
    }
  }
  if (!group.isEmpty()) {
    if (struct group* gr = ::getgrnam(group.toLocal8Bit().constData())) {
      gid = gr->gr_gid;
    } else {
      bool ok = false; gid = group.toUInt(&ok); if (!ok) return false;
    }
  }
  return ::chown(path.toLocal8Bit().constData(), uid, gid) == 0;
}
#endif

} // namespace

PropertiesDialog::PropertiesDialog(const QStringList& paths, QWidget* parent)
  : QDialog(parent)
  , m_paths(paths) {
  setWindowTitle(tr("Properties"));
  resize(680, 0);
  setMinimumWidth(560);
  setModal(true);
  setupUi();
  populateStaticInfo();
  loadAttributes();

  // ディレクトリを含む / 複数選択ではサイズを別スレッドで集計する。
  if (m_aggregateSize) {
    m_sizeLabel->setText(tr("calculating..."));
    m_worker = new PropertiesWorker(m_paths, this);
    connect(m_worker, &PropertiesWorker::statsUpdated,
            this, &PropertiesDialog::onStatsUpdated);
    connect(m_worker, &WorkerBase::finished,
            this, &PropertiesDialog::onWorkerFinished);
    m_worker->start();
  }
}

PropertiesDialog::~PropertiesDialog() {
  // 念のため: ダイアログ終了時にワーカーを停止する。
  if (m_worker) {
    m_worker->requestCancel();
    m_worker->wait(2000);
  }
}

void PropertiesDialog::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);

  m_form = new QFormLayout();
  m_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
  m_form->setRowWrapPolicy(QFormLayout::DontWrapRows);
  m_form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  m_form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);

  auto makeValueLabel = [this]() -> QLabel* {
    auto* lbl = new QLabel(this);
    lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lbl->setWordWrap(true);
    lbl->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    // QFormLayout 内で折り返しの高さ (height-for-width) を行の高さに反映させる。
    QSizePolicy sp = lbl->sizePolicy();
    sp.setHeightForWidth(true);
    sp.setVerticalPolicy(QSizePolicy::Minimum);
    lbl->setSizePolicy(sp);
    return lbl;
  };

  m_pathLabel        = makeValueLabel();   // 折り返して全体表示
  m_typeLabel        = makeValueLabel();
  m_sizeLabel        = makeValueLabel();
  m_createdLabel     = makeValueLabel();
  m_accessedLabel    = makeValueLabel();
  m_linkTargetLabel  = makeValueLabel();

  // 編集可能なフィールド (名前 / 更新日時 / 所有者 / グループ)
  m_nameEdit = new QLineEdit(this);
  m_modifiedEdit = new QDateTimeEdit(this);
  m_modifiedEdit->setDisplayFormat(QStringLiteral("yyyy/MM/dd HH:mm:ss"));
  m_modifiedEdit->setCalendarPopup(true);
  // 所有者 / グループ: 名前は編集可能、ID は表示のみ。名前欄 + "(ID)" ラベルを
  // 1 行にまとめ、行ごとの表示制御のため QWidget でラップする。
  m_ownerEdit = new QLineEdit(this);
  m_ownerIdLabel = new QLabel(this);
  m_ownerWidget = new QWidget(this);
  auto* ownerRow = new QHBoxLayout(m_ownerWidget);
  ownerRow->setContentsMargins(0, 0, 0, 0);
  ownerRow->addWidget(m_ownerEdit, 0);
  ownerRow->addWidget(m_ownerIdLabel, 0);
  ownerRow->addStretch(1);
  m_groupEdit = new QLineEdit(this);
  m_groupIdLabel = new QLabel(this);
  m_groupWidget = new QWidget(this);
  auto* groupRow = new QHBoxLayout(m_groupWidget);
  groupRow->setContentsMargins(0, 0, 0, 0);
  groupRow->addWidget(m_groupEdit, 0);
  groupRow->addWidget(m_groupIdLabel, 0);
  groupRow->addStretch(1);
  // 名前は短いので入力欄も短めにする。
  m_ownerEdit->setMaximumWidth(220);
  m_groupEdit->setMaximumWidth(220);
  // 名前 / パスの表示領域を十分に確保する (値列がこの幅まで広がる)。
  m_nameEdit->setMinimumWidth(520);
  m_pathLabel->setMinimumWidth(520);

  m_form->addRow(tr("Name:"),        m_nameEdit);
  m_form->addRow(tr("Path:"),        m_pathLabel);
  m_form->addRow(tr("Type:"),        m_typeLabel);
  m_form->addRow(tr("Size:"),        m_sizeLabel);
  m_form->addRow(tr("Modified:"),    m_modifiedEdit);
  m_form->addRow(tr("Created:"),     m_createdLabel);
  m_form->addRow(tr("Accessed:"),    m_accessedLabel);
  m_form->addRow(tr("Owner:"),       m_ownerWidget);
  m_form->addRow(tr("Group:"),       m_groupWidget);
  m_form->addRow(tr("Link Target:"), m_linkTargetLabel);

  mainLayout->addLayout(m_form);

  // 変更可能な属性の編集セクション (パーミッション / Windows 属性)。
  buildAttributeEditor(mainLayout);
  mainLayout->addStretch(1);  // 余剰の縦スペースを吸収し、各グループが間延びしないように

  // OK (適用) / Cancel。
  QHBoxLayout* btnLayout = new QHBoxLayout();
  btnLayout->addStretch(1);
  m_cancelButton = new QPushButton(tr("Cancel"), this);
  m_okButton     = new QPushButton(tr("OK"),     this);
  applyAltShortcut(m_cancelButton, Qt::Key_X);
  applyAltShortcut(m_okButton,     Qt::Key_O);
  m_okButton->setDefault(true);
  btnLayout->addWidget(m_cancelButton);
  btnLayout->addWidget(m_okButton);
  mainLayout->addLayout(btnLayout);

  connect(m_okButton,     &QPushButton::clicked, this, &PropertiesDialog::onAccepted);
  connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);

  // 名前 / パスが長く折り返しても各行が潰れず全体が見えるよう、レイアウトの
  // 最小サイズでダイアログを開く (折り返しの高さ計算を正しく効かせる)。
  mainLayout->setSizeConstraint(QLayout::SetMinimumSize);
}

void PropertiesDialog::buildAttributeEditor(QBoxLayout* parentLayout) {
  const bool multi = m_paths.size() > 1;
#ifdef Q_OS_WIN
  auto* group = new QGroupBox(tr("Attributes"), this);
  auto* v = new QVBoxLayout(group);
  m_readOnlyCheck = new QCheckBox(tr("Read-only"), this);
  m_hiddenCheck   = new QCheckBox(tr("Hidden"),    this);
  m_readOnlyCheck->setFocusPolicy(Qt::StrongFocus);
  m_hiddenCheck->setFocusPolicy(Qt::StrongFocus);
  if (multi) {
    m_readOnlyCheck->setTristate(true);
    m_hiddenCheck->setTristate(true);
  }
  v->addWidget(m_readOnlyCheck);
  v->addWidget(m_hiddenCheck);
  setTabOrder(m_readOnlyCheck, m_hiddenCheck);
#else
  auto* group = new QGroupBox(tr("Permissions"), this);
  auto* grid = new QGridLayout(group);
  grid->addWidget(new QLabel(tr("Read"),    this), 0, 1, Qt::AlignHCenter);
  grid->addWidget(new QLabel(tr("Write"),   this), 0, 2, Qt::AlignHCenter);
  grid->addWidget(new QLabel(tr("Execute"), this), 0, 3, Qt::AlignHCenter);
  grid->addWidget(new QLabel(tr("Owner"), this), 1, 0);
  grid->addWidget(new QLabel(tr("Group"), this), 2, 0);
  grid->addWidget(new QLabel(tr("Other"), this), 3, 0);

  m_ownerRead  = new QCheckBox(this);
  m_ownerWrite = new QCheckBox(this);
  m_ownerExec  = new QCheckBox(this);
  m_groupRead  = new QCheckBox(this);
  m_groupWrite = new QCheckBox(this);
  m_groupExec  = new QCheckBox(this);
  m_otherRead  = new QCheckBox(this);
  m_otherWrite = new QCheckBox(this);
  m_otherExec  = new QCheckBox(this);

  for (auto* cb : {m_ownerRead, m_ownerWrite, m_ownerExec,
                   m_groupRead, m_groupWrite, m_groupExec,
                   m_otherRead, m_otherWrite, m_otherExec}) {
    cb->setFocusPolicy(Qt::StrongFocus);
    if (multi) cb->setTristate(true);
  }

  grid->addWidget(m_ownerRead,  1, 1, Qt::AlignHCenter);
  grid->addWidget(m_ownerWrite, 1, 2, Qt::AlignHCenter);
  grid->addWidget(m_ownerExec,  1, 3, Qt::AlignHCenter);
  grid->addWidget(m_groupRead,  2, 1, Qt::AlignHCenter);
  grid->addWidget(m_groupWrite, 2, 2, Qt::AlignHCenter);
  grid->addWidget(m_groupExec,  2, 3, Qt::AlignHCenter);
  grid->addWidget(m_otherRead,  3, 1, Qt::AlignHCenter);
  grid->addWidget(m_otherWrite, 3, 2, Qt::AlignHCenter);
  grid->addWidget(m_otherExec,  3, 3, Qt::AlignHCenter);

  setTabOrder(m_ownerRead,  m_ownerWrite);
  setTabOrder(m_ownerWrite, m_ownerExec);
  setTabOrder(m_ownerExec,  m_groupRead);
  setTabOrder(m_groupRead,  m_groupWrite);
  setTabOrder(m_groupWrite, m_groupExec);
  setTabOrder(m_groupExec,  m_otherRead);
  setTabOrder(m_otherRead,  m_otherWrite);
  setTabOrder(m_otherWrite, m_otherExec);
#endif
  parentLayout->addWidget(group);
}

void PropertiesDialog::populateStaticInfo() {
  const bool multi = m_paths.size() > 1;

  if (multi) {
    m_nameEdit->setText(tr("%1 items").arg(m_paths.size()));
    m_nameEdit->setReadOnly(true);  // 複数選択ではリネーム不可
    m_aggregateSize = true;  // 合計サイズを worker で集計する

    // パス / 種別 / 作成・アクセス日時 / リンク先は複数では意味が薄いので隠す。
    m_form->setRowVisible(m_pathLabel, false);
    for (QLabel* l : {m_typeLabel, m_createdLabel,
                      m_accessedLabel, m_linkTargetLabel}) {
      m_form->setRowVisible(l, false);
    }

    // 更新日時: 一括で変更できる。代表として最初の対象の更新日時を表示する。
    m_origModified = QFileInfo(m_paths.first()).lastModified();
    m_modifiedEdit->setDateTime(m_origModified);

#ifdef Q_OS_WIN
    m_form->setRowVisible(m_ownerWidget, false);
    m_form->setRowVisible(m_groupWidget, false);
#else
    // 所有者 / グループ: 全対象で名前・ID とも共通のときだけ編集可能 (混在は隠す)。
    QString commonOwner, commonGroup, commonOwnerId, commonGroupId;
    bool ownerCommon = true, groupCommon = true;
    for (int i = 0; i < m_paths.size(); ++i) {
      const QFileInfo fi(m_paths.at(i));
      const QString o = fi.owner();
      const QString g = fi.group();
      const QString oid = QString::number(fi.ownerId());
      const QString gid = QString::number(fi.groupId());
      if (i == 0) { commonOwner = o; commonGroup = g; commonOwnerId = oid; commonGroupId = gid; }
      else {
        if (o != commonOwner || oid != commonOwnerId) ownerCommon = false;
        if (g != commonGroup || gid != commonGroupId) groupCommon = false;
      }
    }
    if (ownerCommon) {
      m_origOwner = commonOwner;
      m_ownerEdit->setText(commonOwner);
      m_ownerIdLabel->setText(QStringLiteral("(%1)").arg(commonOwnerId));
      m_ownerEditable = true;
    } else {
      m_form->setRowVisible(m_ownerWidget, false);
    }
    if (groupCommon) {
      m_origGroup = commonGroup;
      m_groupEdit->setText(commonGroup);
      m_groupIdLabel->setText(QStringLiteral("(%1)").arg(commonGroupId));
      m_groupEditable = true;
    } else {
      m_form->setRowVisible(m_groupWidget, false);
    }
#endif
    return;
  }

  // 単一選択
  QFileInfo fi(m_paths.first());
  const bool isDir = fi.isDir() && !fi.isSymLink();
  m_aggregateSize = isDir;

  m_origName = fi.fileName().isEmpty() ? fi.absoluteFilePath() : fi.fileName();
  m_nameEdit->setText(m_origName);
  m_pathLabel->setText(fi.absoluteFilePath());

  QString type;
  if (fi.isSymLink())   type = tr("Symbolic link");
  else if (fi.isDir())  type = tr("Directory");
  else if (fi.isFile()) type = tr("File");
  else                  type = tr("Other");
  m_typeLabel->setText(type);

  if (isDir) {
    m_sizeLabel->setText(tr("calculating..."));
  } else {
    m_sizeLabel->setText(formatSize(fi.size()));
  }

  m_origModified = fi.lastModified();
  m_modifiedEdit->setDateTime(m_origModified);
  m_createdLabel ->setText(formatDate(fi.birthTime()));
  m_accessedLabel->setText(formatDate(fi.lastRead()));

  m_origOwner = fi.owner();  // 名前 (取得できないこともある)。編集対象は名前のみ。
  m_ownerEdit->setText(m_origOwner);
  m_ownerIdLabel->setText(QStringLiteral("(%1)").arg(fi.ownerId()));
  m_origGroup = fi.group();
  m_groupEdit->setText(m_origGroup);
  m_groupIdLabel->setText(QStringLiteral("(%1)").arg(fi.groupId()));

  if (fi.isSymLink()) {
    m_linkTargetLabel->setText(fi.symLinkTarget());
  } else {
    m_linkTargetLabel->setText(QStringLiteral("—"));
  }

  // プラットフォーム別に意味の薄い行を隠す (Windows は ACL ベースで rwx /
  // owner / group が実態と乖離する。Link Target はリンクのときだけ表示)。
#ifdef Q_OS_WIN
  m_form->setRowVisible(m_ownerWidget,      false);
  m_form->setRowVisible(m_groupWidget,      false);
  m_form->setRowVisible(m_linkTargetLabel,  false);
#else
  m_ownerEditable = true;   // 単一選択なので所有者 / グループは編集可能
  m_groupEditable = true;
  if (!fi.isSymLink()) {
    m_form->setRowVisible(m_linkTargetLabel, false);
  }
#endif
}

void PropertiesDialog::loadAttributes() {
  if (m_paths.isEmpty()) return;
#ifdef Q_OS_WIN
  int roTrue = 0, roFalse = 0, hdTrue = 0, hdFalse = 0;
  for (const QString& p : m_paths) {
    DWORD attrs = windowsAttributes(p);
    if (attrs == INVALID_FILE_ATTRIBUTES) continue;
    if (attrs & FILE_ATTRIBUTE_READONLY) ++roTrue; else ++roFalse;
    if (attrs & FILE_ATTRIBUTE_HIDDEN)   ++hdTrue; else ++hdFalse;
  }
  auto tri = [](int t, int f) -> Qt::CheckState {
    if (t > 0 && f == 0) return Qt::Checked;
    if (t == 0 && f > 0) return Qt::Unchecked;
    return Qt::PartiallyChecked;
  };
  m_readOnlyCheck->setCheckState(tri(roTrue, roFalse));
  m_hiddenCheck->setCheckState(tri(hdTrue, hdFalse));
#else
  struct Count { int t = 0; int f = 0; };
  Count oR, oW, oX, gR, gW, gX, xR, xW, xX;
  for (const QString& p : m_paths) {
    QFile f(p);
    const auto perms = f.permissions();
    auto bump = [](Count& c, bool on) { if (on) ++c.t; else ++c.f; };
    bump(oR, perms.testFlag(QFile::ReadOwner)  || perms.testFlag(QFile::ReadUser));
    bump(oW, perms.testFlag(QFile::WriteOwner) || perms.testFlag(QFile::WriteUser));
    bump(oX, perms.testFlag(QFile::ExeOwner)   || perms.testFlag(QFile::ExeUser));
    bump(gR, perms.testFlag(QFile::ReadGroup));
    bump(gW, perms.testFlag(QFile::WriteGroup));
    bump(gX, perms.testFlag(QFile::ExeGroup));
    bump(xR, perms.testFlag(QFile::ReadOther));
    bump(xW, perms.testFlag(QFile::WriteOther));
    bump(xX, perms.testFlag(QFile::ExeOther));
  }
  auto tri = [](const Count& c) -> Qt::CheckState {
    if (c.t > 0 && c.f == 0) return Qt::Checked;
    if (c.t == 0 && c.f > 0) return Qt::Unchecked;
    return Qt::PartiallyChecked;
  };
  m_ownerRead->setCheckState(tri(oR));
  m_ownerWrite->setCheckState(tri(oW));
  m_ownerExec->setCheckState(tri(oX));
  m_groupRead->setCheckState(tri(gR));
  m_groupWrite->setCheckState(tri(gW));
  m_groupExec->setCheckState(tri(gX));
  m_otherRead->setCheckState(tri(xR));
  m_otherWrite->setCheckState(tri(xW));
  m_otherExec->setCheckState(tri(xX));
#endif
}

void PropertiesDialog::applyAttributes() {
#ifdef Q_OS_WIN
  const auto ro = m_readOnlyCheck->checkState();
  const auto hd = m_hiddenCheck->checkState();
  for (const QString& p : m_paths) {
    DWORD attrs = windowsAttributes(p);
    if (attrs == INVALID_FILE_ATTRIBUTES) continue;
    if (ro == Qt::Checked)   attrs |= FILE_ATTRIBUTE_READONLY;
    if (ro == Qt::Unchecked) attrs &= ~FILE_ATTRIBUTE_READONLY;
    if (hd == Qt::Checked)   attrs |= FILE_ATTRIBUTE_HIDDEN;
    if (hd == Qt::Unchecked) attrs &= ~FILE_ATTRIBUTE_HIDDEN;
    SetFileAttributesW(reinterpret_cast<LPCWSTR>(p.utf16()), attrs);
  }
#else
  // PartiallyChecked のフラグは触らない (元の値を維持)。
  // Unix で Owner/User ビットは OR で効くため、Owner 行では両方を同時操作する。
  struct Flag { QCheckBox* cb; QFile::Permissions bits; };
  const Flag flags[] = {
    { m_ownerRead,  QFile::ReadOwner  | QFile::ReadUser  },
    { m_ownerWrite, QFile::WriteOwner | QFile::WriteUser },
    { m_ownerExec,  QFile::ExeOwner   | QFile::ExeUser   },
    { m_groupRead,  QFile::ReadGroup                     },
    { m_groupWrite, QFile::WriteGroup                    },
    { m_groupExec,  QFile::ExeGroup                      },
    { m_otherRead,  QFile::ReadOther                     },
    { m_otherWrite, QFile::WriteOther                    },
    { m_otherExec,  QFile::ExeOther                      },
  };
  for (const QString& p : m_paths) {
    QFile f(p);
    auto perms = f.permissions();
    for (const auto& flag : flags) {
      switch (flag.cb->checkState()) {
        case Qt::Checked:   perms |=  flag.bits; break;
        case Qt::Unchecked: perms &= ~flag.bits; break;
        case Qt::PartiallyChecked: break;  // 触らない
      }
    }
    f.setPermissions(perms);
  }
#endif
}

void PropertiesDialog::onAccepted() {
  applyAttributes();
  applyEditableFields();
  accept();
}

void PropertiesDialog::applyEditableFields() {
  QStringList failures;

  // 更新日時: 変更されていれば対象すべてに適用する (複数選択は一括)。
  const QDateTime newMtime = m_modifiedEdit->dateTime();
  if (newMtime.isValid() && newMtime != m_origModified) {
    for (const QString& p : m_paths) {
      if (!setModificationTime(p, newMtime)) { failures << tr("modification time"); break; }
    }
  }

#ifndef Q_OS_WIN
  // 所有者 / グループ: 編集可能 (単一、または複数で共通) かつ変更時、対象すべてに適用。
  if (m_ownerEditable || m_groupEditable) {
    const QString newOwner = m_ownerEditable ? m_ownerEdit->text().trimmed() : QString();
    const QString newGroup = m_groupEditable ? m_groupEdit->text().trimmed() : QString();
    if ((m_ownerEditable && newOwner != m_origOwner) ||
        (m_groupEditable && newGroup != m_origGroup)) {
      for (const QString& p : m_paths) {
        if (!changeOwnerGroup(p, newOwner, newGroup)) { failures << tr("owner / group"); break; }
      }
    }
  }
#endif

  // 名前 (リネーム): 単一選択時のみ。他の編集を旧パスに適用してから最後に移動する。
  if (m_paths.size() == 1 && !m_nameEdit->isReadOnly()) {
    const QString newName = m_nameEdit->text().trimmed();
    if (!newName.isEmpty() && newName != m_origName) {
      const QFileInfo fi(m_paths.first());
      const QString newPath = fi.absolutePath() + QStringLiteral("/") + newName;
      if (!QFile::rename(m_paths.first(), newPath)) {
        failures << tr("name");
      }
    }
  }

  if (!failures.isEmpty()) {
    warn(this, tr("Could not apply some changes"),
      tr("Failed to change: %1\n(You may not have the required permission.)")
        .arg(failures.join(QStringLiteral(", "))));
  }
}

void PropertiesDialog::onStatsUpdated(qint64 totalBytes, int fileCount, int dirCount) {
  m_sizeLabel->setText(
    tr("%1   (%n file(s), %2 directories)", "", fileCount)
      .arg(formatSize(totalBytes))
      .arg(QLocale(QLocale::English).toString(dirCount)));
}

void PropertiesDialog::onWorkerFinished(bool success) {
  if (!success) {
    // キャンセルされた場合は値をそのままに、注釈を付ける。
    m_sizeLabel->setText(m_sizeLabel->text() + tr("  (cancelled)"));
  }
}

void PropertiesDialog::closeEvent(QCloseEvent* event) {
  if (m_worker) {
    m_worker->requestCancel();
  }
  QDialog::closeEvent(event);
}

} // namespace Farman
