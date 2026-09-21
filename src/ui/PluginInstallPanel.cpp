#include "PluginInstallPanel.h"

#include "settings/Settings.h"
#include "utils/Dialogs.h"

#include <QAbstractItemView>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace Farman {

namespace {

// ドロップされた MIME からローカルファイルのパスだけを取り出す。
QStringList localFilesFromMime(const QMimeData* mime) {
  QStringList files;
  if (!mime || !mime->hasUrls()) return files;
  const QList<QUrl> urls = mime->urls();
  for (const QUrl& url : urls) {
    if (!url.isLocalFile()) continue;
    const QString path = url.toLocalFile();
    if (QFileInfo(path).isFile()) {
      files.append(path);
    }
  }
  return files;
}

QString kindLabel(PluginInstaller::Kind kind) {
  switch (kind) {
    case PluginInstaller::Kind::Viewer:  return PluginInstallPanel::tr("Viewer");
    case PluginInstaller::Kind::Archive: return PluginInstallPanel::tr("Archive");
    case PluginInstaller::Kind::Unknown: break;
  }
  return QString();
}

// "viewers/Foo.dylib" → "Foo.dylib (Viewer)"
QString relPathDisplay(const QString& relPath) {
  const QString sub  = relPath.section(QLatin1Char('/'), 0, 0);
  const QString name = relPath.section(QLatin1Char('/'), 1);
  const PluginInstaller::Kind kind =
    sub == PluginInstaller::kindSubdir(PluginInstaller::Kind::Viewer)
      ? PluginInstaller::Kind::Viewer
      : PluginInstaller::Kind::Archive;
  return QStringLiteral("%1 (%2)").arg(name, kindLabel(kind));
}

} // namespace

PluginInstallPanel::PluginInstallPanel(QWidget* parent)
  : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* row = new QHBoxLayout();
  m_installButton = new QPushButton(tr("Install from File..."), this);
  m_installButton->setAutoDefault(false);
  m_installButton->setToolTip(
    tr("Choose plugin files (.%1) to install. The plugin type (viewer / archive) "
       "is detected automatically.").arg(PluginInstaller::nativeLibrarySuffix()));
  connect(m_installButton, &QPushButton::clicked, this,
          &PluginInstallPanel::chooseFiles);
  row->addWidget(m_installButton);

  auto* hint = new QLabel(
    tr("You can also drop plugin files onto the list above."), this);
  hint->setEnabled(false);
  row->addWidget(hint, 1);
  layout->addLayout(row);

  m_pendingGroup = new QGroupBox(tr("Applied after restarting farman"), this);
  m_pendingLayout = new QVBoxLayout(m_pendingGroup);
  layout->addWidget(m_pendingGroup);

  refresh();
}

void PluginInstallPanel::watchDropTarget(QAbstractItemView* view) {
  if (!view) return;
  view->setAcceptDrops(true);
  view->viewport()->setAcceptDrops(true);
  view->viewport()->installEventFilter(this);
}

bool PluginInstallPanel::eventFilter(QObject* watched, QEvent* event) {
  switch (event->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove: {
      auto* dragEvent = static_cast<QDragMoveEvent*>(event);
      if (!localFilesFromMime(dragEvent->mimeData()).isEmpty()) {
        dragEvent->setDropAction(Qt::CopyAction);
        dragEvent->accept();
      } else {
        dragEvent->ignore();
      }
      return true;
    }
    case QEvent::Drop: {
      auto* dropEvent = static_cast<QDropEvent*>(event);
      const QStringList files = localFilesFromMime(dropEvent->mimeData());
      if (files.isEmpty()) {
        dropEvent->ignore();
        return true;
      }
      dropEvent->setDropAction(Qt::CopyAction);
      dropEvent->accept();
      // ドロップ処理の中でモーダルダイアログを出すとドラッグ元 (Finder 等) を
      // 待たせてしまうので、イベントを返してから導入フローに入る。
      QTimer::singleShot(0, this, [this, files]() { installFiles(files); });
      return true;
    }
    default:
      break;
  }
  return QWidget::eventFilter(watched, event);
}

void PluginInstallPanel::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  // もう一方のタブ (Viewer / Archive) のパネルで行われた操作を取り込む。
  refresh();
}

void PluginInstallPanel::chooseFiles() {
  const QString suffix = PluginInstaller::nativeLibrarySuffix();
  const QStringList files = QFileDialog::getOpenFileNames(
    this, tr("Install Plugins"), QString(),
    tr("farman plugins (*.%1)").arg(suffix));
  if (!files.isEmpty()) {
    installFiles(files);
  }
}

void PluginInstallPanel::installFiles(const QStringList& filePaths) {
  struct Candidate {
    QString               filePath;
    PluginInstaller::Kind kind;
    bool                  replaces;
  };
  const QString root = PluginInstaller::pluginsRoot();
  QList<Candidate> candidates;
  QStringList failures;  // "Foo.dylib: 理由"

  for (const QString& filePath : filePaths) {
    const QString name = QFileInfo(filePath).fileName();
    const PluginInstaller::Inspection inspection =
      PluginInstaller::inspect(filePath, PluginInstaller::hostVersion());
    if (!inspection.ok) {
      failures.append(QStringLiteral("%1: %2").arg(name, inspection.error));
      continue;
    }
    candidates.append({filePath, inspection.kind,
                       PluginInstaller::isInstalledOrPending(root, inspection.kind, name)});
  }

  if (candidates.isEmpty()) {
    warn(this, tr("Install Plugins"),
         tr("No plugin was installed.") + QStringLiteral("\n\n")
           + failures.join(QLatin1Char('\n')));
    return;
  }

  // 導入の確認。ロードしないと制作者を読めず、ハッシュの照合先も無いので、
  // 入手元を信頼できるかはユーザーに判断してもらう。
  QStringList lines;
  for (const Candidate& c : candidates) {
    QString line = QStringLiteral("• %1 (%2)")
                     .arg(QFileInfo(c.filePath).fileName(), kindLabel(c.kind));
    if (c.replaces) {
      line += QStringLiteral(" — ") + tr("replaces the installed file");
    }
    lines.append(line);
  }
  const QString question =
    tr("Install the following plugins?") + QStringLiteral("\n\n")
    + lines.join(QLatin1Char('\n')) + QStringLiteral("\n\n")
    + tr("Plugins run as part of farman and can access your files. farman cannot "
         "verify who made these files, so install only plugins obtained from a "
         "source you trust.");
  if (!confirm(this, tr("Install Plugins"), question, /*defaultYes=*/false)) {
    return;
  }

  // 外部プラグインの読込みが OFF のままだと導入しても動かない。黙って ON には
  // せず、ここで尋ねる。断っても導入は続ける (一覧には「ブロック中」で出る)。
  Settings& settings = Settings::instance();
  if (!settings.allowExternalPlugins()) {
    const bool enable = confirm(
      this, tr("Install Plugins"),
      tr("Loading external plugins is currently turned off, so the installed "
         "plugins will not be loaded.\n\nTurn on \"Allow loading external "
         "plugins\"?"),
      /*defaultYes=*/true);
    if (enable) {
      settings.setAllowExternalPlugins(true);
      settings.save();
      emit allowExternalPluginsEnabled();
    }
  }

  int staged = 0;
  for (const Candidate& c : candidates) {
    QString error;
    if (PluginInstaller::stageInstall(root, c.filePath, c.kind, &error)) {
      ++staged;
    } else {
      failures.append(
        QStringLiteral("%1: %2").arg(QFileInfo(c.filePath).fileName(), error));
    }
  }

  refresh();
  emit pendingChanged();

  QString message;
  if (staged > 0) {
    message = tr("%n plugin(s) will be installed when farman is restarted.", "", staged);
  } else {
    message = tr("No plugin was installed.");
  }
  if (failures.isEmpty()) {
    inform(this, tr("Install Plugins"), message);
  } else {
    warn(this, tr("Install Plugins"),
         message + QStringLiteral("\n\n") + tr("Not installed:") + QLatin1Char('\n')
           + failures.join(QLatin1Char('\n')));
  }
}

bool PluginInstallPanel::isManagedPluginFile(const QString& installedFilePath) {
  return !relativePathOf(installedFilePath).isEmpty();
}

bool PluginInstallPanel::isPendingRemoval(const QString& installedFilePath) const {
  const QString relPath = relativePathOf(installedFilePath);
  return !relPath.isEmpty() && m_pending.removals.contains(relPath);
}

bool PluginInstallPanel::isPendingUpdate(const QString& installedFilePath) const {
  const QString relPath = relativePathOf(installedFilePath);
  return !relPath.isEmpty() && m_pending.installs.contains(relPath);
}

bool PluginInstallPanel::requestUninstall(const QString& installedFilePath,
                                          const QString& displayName) {
  const bool ok = confirm(
    this, tr("Uninstall Plugin"),
    tr("Uninstall \"%1\"?\n\nThe plugin file is deleted when farman is restarted. "
       "Until then you can cancel this from the plugin list.").arg(displayName),
    /*defaultYes=*/false);
  if (!ok) return false;

  QString error;
  if (!PluginInstaller::stageRemoval(PluginInstaller::pluginsRoot(),
                                     installedFilePath, &error)) {
    warn(this, tr("Uninstall Plugin"), error);
    return false;
  }
  refresh();
  emit pendingChanged();
  return true;
}

bool PluginInstallPanel::cancelUninstall(const QString& installedFilePath) {
  const QString relPath = relativePathOf(installedFilePath);
  if (relPath.isEmpty()) return false;
  const bool ok = PluginInstaller::cancelPendingRemoval(
    PluginInstaller::pluginsRoot(), relPath);
  refresh();
  emit pendingChanged();
  return ok;
}

void PluginInstallPanel::refresh() {
  m_pending = PluginInstaller::pendingState(PluginInstaller::pluginsRoot());
  rebuildPendingList();
}

void PluginInstallPanel::rebuildPendingList() {
  while (QLayoutItem* item = m_pendingLayout->takeAt(0)) {
    if (QLayout* childLayout = item->layout()) {
      while (QLayoutItem* child = childLayout->takeAt(0)) {
        delete child->widget();
        delete child;
      }
    }
    delete item->widget();
    delete item;
  }

  const auto addRow = [this](const QString& text, const QString& relPath, bool isInstall) {
    auto* row = new QHBoxLayout();
    auto* label = new QLabel(text, m_pendingGroup);
    label->setToolTip(relPath);
    row->addWidget(label, 1);
    auto* cancel = new QToolButton(m_pendingGroup);
    cancel->setText(tr("Cancel"));
    cancel->setToolTip(isInstall ? tr("Do not install this plugin.")
                                 : tr("Keep this plugin installed."));
    connect(cancel, &QToolButton::clicked, this, [this, relPath, isInstall]() {
      const QString root = PluginInstaller::pluginsRoot();
      if (isInstall) {
        PluginInstaller::cancelPendingInstall(root, relPath);
      } else {
        PluginInstaller::cancelPendingRemoval(root, relPath);
      }
      // ボタン自身がこの一覧の再構築で破棄されるので、シグナル処理を抜けてから行う。
      QTimer::singleShot(0, this, [this]() {
        refresh();
        emit pendingChanged();
      });
    });
    row->addWidget(cancel);
    m_pendingLayout->addLayout(row);
  };

  for (const QString& relPath : m_pending.installs) {
    addRow(tr("Install: %1").arg(relPathDisplay(relPath)), relPath, /*isInstall=*/true);
  }
  for (const QString& relPath : m_pending.removals) {
    addRow(tr("Uninstall: %1").arg(relPathDisplay(relPath)), relPath, /*isInstall=*/false);
  }

  m_pendingGroup->setVisible(!m_pending.installs.isEmpty()
                             || !m_pending.removals.isEmpty());
}

QString PluginInstallPanel::relativePathOf(const QString& installedFilePath) {
  return PluginInstaller::managedRelativePath(PluginInstaller::pluginsRoot(),
                                              installedFilePath);
}

} // namespace Farman
