#include "GeneralTab.h"
#include "settings/Settings.h"
#include "keybinding/CommandRegistry.h"
#include "utils/Dialogs.h"
#include <QDateTime>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QToolButton>
#include <QFileDialog>
#include <QDir>
#include <QLabel>
#include <QStyle>

namespace Farman {

GeneralTab::GeneralTab(const QString& leftCurrentPath,
                       const QString& rightCurrentPath,
                       const QSize&   currentWindowSize,
                       const QPoint&  currentWindowPosition,
                       QWidget* parent)
  : QWidget(parent)
  , m_leftCurrentPath(leftCurrentPath)
  , m_rightCurrentPath(rightCurrentPath)
  , m_currentWindowSize(currentWindowSize)
  , m_currentWindowPosition(currentWindowPosition) {
  setupUi();
  loadSettings();
}

void GeneralTab::setupUi() {
  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // ── Initial Directory + Confirm/Language ─────────────────
  QGroupBox* startupGroup = new QGroupBox(tr("Startup Settings"), this);
  QVBoxLayout* startupLayout = new QVBoxLayout(startupGroup);

  QGroupBox* initialPathsGroup = new QGroupBox(tr("Initial Directory"), this);
  QHBoxLayout* initialPathsLayout = new QHBoxLayout(initialPathsGroup);

  auto buildPanePathBox = [this](
      const QString& title,
      QComboBox*& modeCombo,
      QLineEdit*& pathEdit,
      QToolButton*& browseBtn) -> QGroupBox* {
    QGroupBox* box = new QGroupBox(title, this);
    QFormLayout* form = new QFormLayout(box);
    // Field 列がウィジェットの sizeHint で頭打ちにならないよう、
    // フォーム全体で「水平方向に広げる」ポリシーを使う。
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    modeCombo = new QComboBox(this);
    modeCombo->addItem(tr("Default (Home)"),   static_cast<int>(InitialPathMode::Default));
    modeCombo->addItem(tr("Last Session"),     static_cast<int>(InitialPathMode::LastSession));
    modeCombo->addItem(tr("Custom"),           static_cast<int>(InitialPathMode::Custom));
    form->addRow(tr("Mode:"), modeCombo);

    QWidget* pathRow = new QWidget(this);
    QHBoxLayout* pathRowLayout = new QHBoxLayout(pathRow);
    pathRowLayout->setContentsMargins(0, 0, 0, 0);
    pathEdit = new QLineEdit(this);
    pathEdit->setPlaceholderText(tr("/path/to/directory"));
    pathEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    browseBtn = new QToolButton(this);
    browseBtn->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
    browseBtn->setToolTip(tr("Browse for folder..."));
    pathRowLayout->addWidget(pathEdit, 1);
    pathRowLayout->addWidget(browseBtn);
    form->addRow(tr("Custom Path:"), pathRow);

    return box;
  };

  initialPathsLayout->addWidget(buildPanePathBox(
    tr("Left Pane"),
    m_leftInitialPathModeCombo, m_leftCustomPathEdit, m_leftBrowseButton));
  initialPathsLayout->addWidget(buildPanePathBox(
    tr("Right Pane"),
    m_rightInitialPathModeCombo, m_rightCustomPathEdit, m_rightBrowseButton));

  connect(m_leftInitialPathModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &GeneralTab::onLeftInitialPathModeChanged);
  connect(m_rightInitialPathModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &GeneralTab::onRightInitialPathModeChanged);
  connect(m_leftBrowseButton,  &QToolButton::clicked,
          this, &GeneralTab::onLeftBrowseInitialPath);
  connect(m_rightBrowseButton, &QToolButton::clicked,
          this, &GeneralTab::onRightBrowseInitialPath);

  startupLayout->addWidget(initialPathsGroup);

  // ── Window settings (Startup の中、Initial Directory の下) ──
  QGroupBox* windowGroup = new QGroupBox(tr("Window Settings"), this);
  QHBoxLayout* windowLayout = new QHBoxLayout(windowGroup);

  QGroupBox* sizeGroup = new QGroupBox(tr("Size"), this);
  QFormLayout* sizeFormLayout = new QFormLayout(sizeGroup);

  m_windowSizeModeCombo = new QComboBox(this);
  m_windowSizeModeCombo->addItem(tr("Default (1200x600)"), static_cast<int>(WindowSizeMode::Default));
  m_windowSizeModeCombo->addItem(tr("Last Session"), static_cast<int>(WindowSizeMode::LastSession));
  m_windowSizeModeCombo->addItem(tr("Custom"), static_cast<int>(WindowSizeMode::Custom));
  connect(m_windowSizeModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &GeneralTab::onWindowSizeModeChanged);
  sizeFormLayout->addRow(tr("Window Size:"), m_windowSizeModeCombo);

  QWidget* sizeWidget = new QWidget(this);
  QHBoxLayout* sizeLayout = new QHBoxLayout(sizeWidget);
  sizeLayout->setContentsMargins(0, 0, 0, 0);
  m_windowWidthSpin = new QSpinBox(this);
  m_windowWidthSpin->setRange(800, 9999);
  m_windowWidthSpin->setSuffix(tr(" px"));
  sizeLayout->addWidget(new QLabel(tr("Width:"), this));
  sizeLayout->addWidget(m_windowWidthSpin);
  m_windowHeightSpin = new QSpinBox(this);
  m_windowHeightSpin->setRange(600, 9999);
  m_windowHeightSpin->setSuffix(tr(" px"));
  sizeLayout->addWidget(new QLabel(tr("Height:"), this));
  sizeLayout->addWidget(m_windowHeightSpin);
  sizeLayout->addStretch();
  sizeFormLayout->addRow(tr("Custom Size:"), sizeWidget);

  QGroupBox* posGroup = new QGroupBox(tr("Position"), this);
  QFormLayout* posFormLayout = new QFormLayout(posGroup);

  m_windowPositionModeCombo = new QComboBox(this);
  m_windowPositionModeCombo->addItem(tr("Default (Center)"), static_cast<int>(WindowPositionMode::Default));
  m_windowPositionModeCombo->addItem(tr("Last Session"), static_cast<int>(WindowPositionMode::LastSession));
  m_windowPositionModeCombo->addItem(tr("Custom"), static_cast<int>(WindowPositionMode::Custom));
  connect(m_windowPositionModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &GeneralTab::onWindowPositionModeChanged);
  posFormLayout->addRow(tr("Window Position:"), m_windowPositionModeCombo);

  QWidget* posWidget = new QWidget(this);
  QHBoxLayout* posLayout = new QHBoxLayout(posWidget);
  posLayout->setContentsMargins(0, 0, 0, 0);
  m_windowXSpin = new QSpinBox(this);
  m_windowXSpin->setRange(0, 9999);
  m_windowXSpin->setSuffix(tr(" px"));
  posLayout->addWidget(new QLabel(tr("X:"), this));
  posLayout->addWidget(m_windowXSpin);
  m_windowYSpin = new QSpinBox(this);
  m_windowYSpin->setRange(0, 9999);
  m_windowYSpin->setSuffix(tr(" px"));
  posLayout->addWidget(new QLabel(tr("Y:"), this));
  posLayout->addWidget(m_windowYSpin);
  posLayout->addStretch();
  posFormLayout->addRow(tr("Custom Position:"), posWidget);

  windowLayout->addWidget(sizeGroup);
  windowLayout->addWidget(posGroup);
  startupLayout->addWidget(windowGroup);

  mainLayout->addWidget(startupGroup);

  // ── Log settings ────────────────────────────────
  QGroupBox* logGroup = new QGroupBox(tr("Log"), this);
  QVBoxLayout* logGroupLayout = new QVBoxLayout(logGroup);

  m_logVisibleCheck = new QCheckBox(tr("Show log pane"), this);
  m_logVisibleCheck->setToolTip(
    tr("Show the log pane between the file panes and status bar. "
       "Toggle from the View menu or with Ctrl+L."));

  m_logPaneHeightSpin = new QSpinBox(this);
  m_logPaneHeightSpin->setRange(40, 4000);
  m_logPaneHeightSpin->setSingleStep(10);
  m_logPaneHeightSpin->setSuffix(tr(" px"));
  m_logPaneHeightSpin->setToolTip(tr("Fixed height of the log pane in pixels."));

  // 1 行目: ログパネルの表示設定 (パネル表示 / 高さ)。
  QHBoxLayout* logVisibleRow = new QHBoxLayout();
  logVisibleRow->setContentsMargins(0, 0, 0, 0);
  logVisibleRow->addWidget(m_logVisibleCheck);
  logVisibleRow->addSpacing(16);
  logVisibleRow->addWidget(new QLabel(tr("Height:"), this));
  logVisibleRow->addWidget(m_logPaneHeightSpin);
  logVisibleRow->addStretch(1);
  logGroupLayout->addLayout(logVisibleRow);

  m_logToFileCheck = new QCheckBox(tr("Write log to file"), this);
  m_logToFileCheck->setToolTip(
    tr("Append log entries to a date-stamped file (rotated daily) in addition to the log pane."));

  m_logDirectoryEdit = new QLineEdit(this);
  m_logDirectoryEdit->setPlaceholderText(tr("/path/to/log/dir"));
  m_logDirectoryEdit->setToolTip(
    tr("Directory where daily log files (farman-YYYY-MM-DD.log) are stored."));

  m_logDirectoryBrowse = new QToolButton(this);
  m_logDirectoryBrowse->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
  m_logDirectoryBrowse->setToolTip(tr("Choose log directory..."));

  // 2 行目: ログをファイルに保存 (有効時のみ下の保持設定が効く)。
  QHBoxLayout* logFileRow = new QHBoxLayout();
  logFileRow->setContentsMargins(0, 0, 0, 0);
  logFileRow->addWidget(m_logToFileCheck);
  logFileRow->addWidget(new QLabel(tr("Directory:"), this));
  logFileRow->addWidget(m_logDirectoryEdit, 1);
  logFileRow->addWidget(m_logDirectoryBrowse);
  logGroupLayout->addLayout(logFileRow);

  // 3 行目: 保存日数 / 永久に保持。ファイル保存が有効なときだけ意味を持つ
  // 設定なので、「ログをファイルに保存」の下に字下げして配置する。
  // Tab 順を表示順に合わせるため、ウィジェットもここ (ファイル保存の後) で生成する。
  m_logRetentionDaysSpin = new QSpinBox(this);
  m_logRetentionDaysSpin->setRange(1, 36500);
  m_logRetentionDaysSpin->setSuffix(tr(" days"));
  m_logRetentionDaysSpin->setToolTip(
    tr("Daily log files older than this number of days are deleted on startup and on rotation."));

  m_logRetentionForeverCheck = new QCheckBox(tr("Keep forever"), this);
  m_logRetentionForeverCheck->setToolTip(
    tr("If checked, never delete old daily log files."));

  QHBoxLayout* logRetentionRow = new QHBoxLayout();
  logRetentionRow->setContentsMargins(0, 0, 0, 0);
  logRetentionRow->addSpacing(24);
  logRetentionRow->addWidget(new QLabel(tr("Retention:"), this));
  logRetentionRow->addWidget(m_logRetentionDaysSpin);
  logRetentionRow->addSpacing(8);
  logRetentionRow->addWidget(m_logRetentionForeverCheck);
  logRetentionRow->addStretch(1);
  logGroupLayout->addLayout(logRetentionRow);

  auto updateLogFileEnabled = [this]() {
    const bool fileEnabled = m_logToFileCheck->isChecked();
    m_logDirectoryEdit->setEnabled(fileEnabled);
    m_logDirectoryBrowse->setEnabled(fileEnabled);
    m_logRetentionForeverCheck->setEnabled(fileEnabled);
    m_logRetentionDaysSpin->setEnabled(fileEnabled && !m_logRetentionForeverCheck->isChecked());
  };
  connect(m_logToFileCheck, &QCheckBox::toggled, this, updateLogFileEnabled);
  connect(m_logRetentionForeverCheck, &QCheckBox::toggled, this, updateLogFileEnabled);
  connect(m_logDirectoryBrowse, &QToolButton::clicked, this, [this]() {
    const QString start = m_logDirectoryEdit->text().isEmpty()
                          ? QDir::homePath()
                          : m_logDirectoryEdit->text();
    const QString selected = QFileDialog::getExistingDirectory(
      this, tr("Choose log directory"), start,
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    if (!selected.isEmpty()) {
      m_logDirectoryEdit->setText(selected);
    }
  });

  mainLayout->addWidget(logGroup);

  // Log の下: Confirm on exit / Language
  // (Tab 順を「ログ → 終了時確認 → 言語」にするため、Confirm/Language は
  //  ログのウィジェットを構築し終えてからここで生成する)
  m_confirmOnExitCheck = new QCheckBox(tr("Confirm on exit"), this);
  m_confirmOnExitCheck->setToolTip(tr("Show confirmation dialog when closing the application"));
  mainLayout->addWidget(m_confirmOnExitCheck);

  m_singleInstanceCheck = new QCheckBox(
    tr("Prevent multiple instances (takes effect on next launch)"), this);
  m_singleInstanceCheck->setToolTip(
    tr("If on, launching farman while another instance is already running "
       "will bring the existing window to the front instead of starting a "
       "new process. Off allows multiple parallel instances."));
  mainLayout->addWidget(m_singleInstanceCheck);

  m_showToolbarCheck = new QCheckBox(tr("Show toolbar"), this);
  m_showToolbarCheck->setToolTip(
    tr("Show the icon toolbar under the menu bar. Can also be toggled from "
       "the View menu."));
  mainLayout->addWidget(m_showToolbarCheck);

  m_languageCombo = new QComboBox(this);
  m_languageCombo->addItem(tr("Auto (System)"), static_cast<int>(LanguageMode::Auto));
  m_languageCombo->addItem(tr("English"),       static_cast<int>(LanguageMode::English));
  m_languageCombo->addItem(QStringLiteral("日本語 (Japanese)"),
                           static_cast<int>(LanguageMode::Japanese));
  m_languageCombo->setToolTip(tr("Takes effect on next launch."));

  QHBoxLayout* languageRow = new QHBoxLayout();
  languageRow->setContentsMargins(0, 0, 0, 0);
  languageRow->addWidget(new QLabel(tr("Language:"), this));
  languageRow->addWidget(m_languageCombo);
  // 「再起動後に有効」の補足を可視化 (combo のツールチップだけだと
  //  気付きにくいので、設定行に直接書いておく)
  languageRow->addWidget(new QLabel(tr("(takes effect on next launch)"), this));
  languageRow->addStretch(1);
  mainLayout->addLayout(languageRow);

  // ─── Auto-Update group ─────────────────────────
  // GitHub Releases から最新 stable 版を検出 → ダイアログで通知 → ダウンロード +
  // インストールまで自動化する。SPEC.md "自動アップデート" 節参照。
  QGroupBox* autoUpdateGroup = new QGroupBox(tr("Auto-Update"), this);
  QVBoxLayout* auLayout = new QVBoxLayout(autoUpdateGroup);

  m_autoUpdateCheckOnStartupCheck = new QCheckBox(
    tr("Check for updates automatically (once per day)"), autoUpdateGroup);
  m_autoUpdateCheckOnStartupCheck->setToolTip(
    tr("On startup, query GitHub for a newer release (at most once every 24 h)."
       " If a newer version is found, a notification dialog is shown."));
  auLayout->addWidget(m_autoUpdateCheckOnStartupCheck);

  m_autoUpdateSilentCheck = new QCheckBox(
    tr("Download and install updates without asking"), autoUpdateGroup);
  m_autoUpdateSilentCheck->setToolTip(
    tr("Skip the confirmation dialog when an update is found. The new version "
       "is downloaded, verified by SHA256, and installed automatically."));
  auLayout->addWidget(m_autoUpdateSilentCheck);

  // 1 個目を外したら 2 個目を grey out
  connect(m_autoUpdateCheckOnStartupCheck, &QCheckBox::toggled,
          this, [this](bool on) {
    if (m_autoUpdateSilentCheck) m_autoUpdateSilentCheck->setEnabled(on);
  });

  // 最終チェック日時 + 「今すぐチェック」
  QHBoxLayout* auActionRow = new QHBoxLayout();
  auActionRow->setContentsMargins(0, 0, 0, 0);
  m_autoUpdateLastCheckedLabel = new QLabel(autoUpdateGroup);
  auActionRow->addWidget(m_autoUpdateLastCheckedLabel, 1);
  m_autoUpdateCheckNowButton = new QToolButton(autoUpdateGroup);
  m_autoUpdateCheckNowButton->setText(tr("Check for Updates Now"));
  m_autoUpdateCheckNowButton->setToolTip(
    tr("Run an update check right now (same as Help → Check for Updates...)."));
  auActionRow->addWidget(m_autoUpdateCheckNowButton);
  // ボタン押下時は CommandRegistry 経由で同じ手動チェックコマンドを実行する。
  connect(m_autoUpdateCheckNowButton, &QToolButton::clicked, this, []() {
    CommandRegistry::instance().execute(QStringLiteral("app.check_for_updates"));
  });
  auLayout->addLayout(auActionRow);

  mainLayout->addWidget(autoUpdateGroup);

  // ── 設定全体のエクスポート / インポート (マシン間移行用) ──
  // ブックマーク / ユーザ定義コマンド / カラースキーム / 全ての Settings
  // 値を 1 ファイルにまとめて移行できる。中身は settings.json そのもの
  // (= フォーマットや version 互換は既存の load/save に揃う)。
  QGroupBox* backupGroup = new QGroupBox(tr("Backup / Restore"), this);
  QVBoxLayout* backupLayout = new QVBoxLayout(backupGroup);
  QLabel* backupHint = new QLabel(
    tr("Export all settings (bookmarks, custom commands, color schemes, "
       "viewer options, paths, ...) to a single JSON file you can copy to "
       "another machine. Import replaces every value with the file's content "
       "and reloads the dialog."),
    backupGroup);
  backupHint->setWordWrap(true);
  backupLayout->addWidget(backupHint);

  QHBoxLayout* backupBtnRow = new QHBoxLayout();
  backupBtnRow->addStretch(1);
  QPushButton* exportAllBtn = new QPushButton(tr("Export Settings..."), backupGroup);
  exportAllBtn->setFocusPolicy(Qt::StrongFocus);
  QPushButton* importAllBtn = new QPushButton(tr("Import Settings..."), backupGroup);
  importAllBtn->setFocusPolicy(Qt::StrongFocus);
  backupBtnRow->addWidget(exportAllBtn);
  backupBtnRow->addWidget(importAllBtn);
  backupLayout->addLayout(backupBtnRow);

  connect(exportAllBtn, &QPushButton::clicked, this, &GeneralTab::onExportAllSettings);
  connect(importAllBtn, &QPushButton::clicked, this, &GeneralTab::onImportAllSettings);

  mainLayout->addWidget(backupGroup);

  mainLayout->addStretch();
}

void GeneralTab::loadSettings() {
  auto& settings = Settings::instance();

  auto selectModeByData = [](QComboBox* combo, int value) {
    for (int i = 0; i < combo->count(); ++i) {
      if (combo->itemData(i).toInt() == value) {
        combo->setCurrentIndex(i);
        return;
      }
    }
  };
  selectModeByData(m_leftInitialPathModeCombo,
                   static_cast<int>(settings.initialPathMode(PaneType::Left)));
  selectModeByData(m_rightInitialPathModeCombo,
                   static_cast<int>(settings.initialPathMode(PaneType::Right)));
  onLeftInitialPathModeChanged(m_leftInitialPathModeCombo->currentIndex());
  onRightInitialPathModeChanged(m_rightInitialPathModeCombo->currentIndex());

  m_confirmOnExitCheck->setChecked(settings.confirmOnExit());
  m_singleInstanceCheck->setChecked(settings.singleInstance());
  m_showToolbarCheck->setChecked(settings.showToolbar());
  for (int i = 0; i < m_languageCombo->count(); ++i) {
    if (m_languageCombo->itemData(i).toInt() == static_cast<int>(settings.language())) {
      m_languageCombo->setCurrentIndex(i);
      break;
    }
  }

  for (int i = 0; i < m_windowSizeModeCombo->count(); ++i) {
    if (m_windowSizeModeCombo->itemData(i).toInt() == static_cast<int>(settings.windowSizeMode())) {
      m_windowSizeModeCombo->setCurrentIndex(i);
      break;
    }
  }
  // SpinBox は「現在のウィンドウ」の値で初期化する。モードが Custom 以外でも
  // 同じく現状を見せるので、ユーザーがそのまま Custom に切り替えれば「今の
  // サイズ・位置」がそのまま採用される直感的な動きになる。
  m_windowWidthSpin->setValue(m_currentWindowSize.width());
  m_windowHeightSpin->setValue(m_currentWindowSize.height());

  for (int i = 0; i < m_windowPositionModeCombo->count(); ++i) {
    if (m_windowPositionModeCombo->itemData(i).toInt() == static_cast<int>(settings.windowPositionMode())) {
      m_windowPositionModeCombo->setCurrentIndex(i);
      break;
    }
  }
  m_windowXSpin->setValue(m_currentWindowPosition.x());
  m_windowYSpin->setValue(m_currentWindowPosition.y());

  onWindowSizeModeChanged(m_windowSizeModeCombo->currentIndex());
  onWindowPositionModeChanged(m_windowPositionModeCombo->currentIndex());

  // Auto-Update
  m_autoUpdateCheckOnStartupCheck->setChecked(settings.autoUpdateCheckOnStartup());
  m_autoUpdateSilentCheck->setChecked(settings.autoUpdateSilent());
  m_autoUpdateSilentCheck->setEnabled(settings.autoUpdateCheckOnStartup());
  {
    const QDateTime last = settings.autoUpdateLastCheckedAt();
    if (last.isValid()) {
      m_autoUpdateLastCheckedLabel->setText(
        tr("Last checked: %1").arg(last.toLocalTime().toString(
          QStringLiteral("yyyy-MM-dd HH:mm"))));
    } else {
      m_autoUpdateLastCheckedLabel->setText(tr("Last checked: (never)"));
    }
  }

  m_logVisibleCheck->setChecked(settings.logVisible());
  m_logPaneHeightSpin->setValue(settings.logPaneHeight());
  m_logToFileCheck->setChecked(settings.logToFile());
  m_logDirectoryEdit->setText(settings.logDirectory());
  const int retention = settings.logRetentionDays();
  m_logRetentionForeverCheck->setChecked(retention == 0);
  m_logRetentionDaysSpin->setValue(retention > 0 ? retention : 7);
  const bool logFileEnabled = m_logToFileCheck->isChecked();
  m_logDirectoryEdit->setEnabled(logFileEnabled);
  m_logDirectoryBrowse->setEnabled(logFileEnabled);
  m_logRetentionForeverCheck->setEnabled(logFileEnabled);
  m_logRetentionDaysSpin->setEnabled(logFileEnabled && !m_logRetentionForeverCheck->isChecked());
}

void GeneralTab::save() {
  auto& settings = Settings::instance();

  // 言語の変更検知 (再起動確認のため、setLanguage の前に比較する)
  const LanguageMode newLanguage =
    static_cast<LanguageMode>(m_languageCombo->currentData().toInt());
  m_languageChangedOnSave = (newLanguage != settings.language());

  settings.setInitialPathMode(
    PaneType::Left,
    static_cast<InitialPathMode>(m_leftInitialPathModeCombo->currentData().toInt())
  );
  settings.setInitialPathMode(
    PaneType::Right,
    static_cast<InitialPathMode>(m_rightInitialPathModeCombo->currentData().toInt())
  );
  settings.setCustomInitialPath(PaneType::Left,  m_leftCustomPathEdit->text().trimmed());
  settings.setCustomInitialPath(PaneType::Right, m_rightCustomPathEdit->text().trimmed());
  settings.setConfirmOnExit(m_confirmOnExitCheck->isChecked());
  settings.setSingleInstance(m_singleInstanceCheck->isChecked());
  settings.setShowToolbar(m_showToolbarCheck->isChecked());
  settings.setLanguage(static_cast<LanguageMode>(m_languageCombo->currentData().toInt()));

  WindowSizeMode sizeMode = static_cast<WindowSizeMode>(m_windowSizeModeCombo->currentData().toInt());
  settings.setWindowSizeMode(sizeMode);
  settings.setCustomWindowSize(QSize(m_windowWidthSpin->value(), m_windowHeightSpin->value()));

  WindowPositionMode posMode = static_cast<WindowPositionMode>(m_windowPositionModeCombo->currentData().toInt());
  settings.setWindowPositionMode(posMode);
  settings.setCustomWindowPosition(QPoint(m_windowXSpin->value(), m_windowYSpin->value()));

  // Auto-Update
  settings.setAutoUpdateCheckOnStartup(m_autoUpdateCheckOnStartupCheck->isChecked());
  settings.setAutoUpdateSilent(m_autoUpdateSilentCheck->isChecked());

  settings.setLogVisible(m_logVisibleCheck->isChecked());
  settings.setLogPaneHeight(m_logPaneHeightSpin->value());
  settings.setLogToFile(m_logToFileCheck->isChecked());
  settings.setLogDirectory(m_logDirectoryEdit->text().trimmed());
  settings.setLogRetentionDays(
    m_logRetentionForeverCheck->isChecked() ? 0 : m_logRetentionDaysSpin->value());
}

void GeneralTab::onWindowSizeModeChanged(int index) {
  WindowSizeMode mode = static_cast<WindowSizeMode>(m_windowSizeModeCombo->itemData(index).toInt());
  bool enableCustomSize = (mode == WindowSizeMode::Custom);
  m_windowWidthSpin->setEnabled(enableCustomSize);
  m_windowHeightSpin->setEnabled(enableCustomSize);
}

void GeneralTab::onWindowPositionModeChanged(int index) {
  WindowPositionMode mode = static_cast<WindowPositionMode>(m_windowPositionModeCombo->itemData(index).toInt());
  bool enableCustomPos = (mode == WindowPositionMode::Custom);
  m_windowXSpin->setEnabled(enableCustomPos);
  m_windowYSpin->setEnabled(enableCustomPos);
}

void GeneralTab::onLeftInitialPathModeChanged(int index) {
  InitialPathMode mode = static_cast<InitialPathMode>(
    m_leftInitialPathModeCombo->itemData(index).toInt());
  const bool enableCustom = (mode == InitialPathMode::Custom);
  m_leftCustomPathEdit->setEnabled(enableCustom);
  m_leftBrowseButton->setEnabled(enableCustom);

  const QString saved = Settings::instance().customInitialPath(PaneType::Left);
  if (mode == InitialPathMode::Custom && !saved.isEmpty()) {
    m_leftCustomPathEdit->setText(saved);
  } else {
    m_leftCustomPathEdit->setText(m_leftCurrentPath);
  }
}

void GeneralTab::onRightInitialPathModeChanged(int index) {
  InitialPathMode mode = static_cast<InitialPathMode>(
    m_rightInitialPathModeCombo->itemData(index).toInt());
  const bool enableCustom = (mode == InitialPathMode::Custom);
  m_rightCustomPathEdit->setEnabled(enableCustom);
  m_rightBrowseButton->setEnabled(enableCustom);

  const QString saved = Settings::instance().customInitialPath(PaneType::Right);
  if (mode == InitialPathMode::Custom && !saved.isEmpty()) {
    m_rightCustomPathEdit->setText(saved);
  } else {
    m_rightCustomPathEdit->setText(m_rightCurrentPath);
  }
}

void GeneralTab::onLeftBrowseInitialPath() {
  const QString start = m_leftCustomPathEdit->text().isEmpty()
                        ? QDir::homePath()
                        : m_leftCustomPathEdit->text();
  const QString selected = QFileDialog::getExistingDirectory(
    this, tr("Select initial directory for left pane"), start,
    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
  );
  if (!selected.isEmpty()) {
    m_leftCustomPathEdit->setText(selected);
  }
}

void GeneralTab::onRightBrowseInitialPath() {
  const QString start = m_rightCustomPathEdit->text().isEmpty()
                        ? QDir::homePath()
                        : m_rightCustomPathEdit->text();
  const QString selected = QFileDialog::getExistingDirectory(
    this, tr("Select initial directory for right pane"), start,
    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
  );
  if (!selected.isEmpty()) {
    m_rightCustomPathEdit->setText(selected);
  }
}

void GeneralTab::onExportAllSettings() {
  // 出力先のデフォルトはユーザの Documents 下、ファイル名は farman-settings-
  // <yyyyMMdd-HHmm>.json。
  const QString defaultDir =
    QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  const QString defaultName = QStringLiteral("farman-settings-%1.json")
    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm")));
  const QString path = QFileDialog::getSaveFileName(
    this, tr("Export All Settings"),
    QDir(defaultDir).filePath(defaultName),
    tr("JSON Files (*.json)"));
  if (path.isEmpty()) return;

  QString err;
  if (!Settings::instance().exportTo(path, &err)) {
    warn(this, tr("Export Settings"), err);
    return;
  }
  inform(this, tr("Export Settings"),
         tr("Settings exported to:\n%1").arg(path));
}

void GeneralTab::onImportAllSettings() {
  const QString defaultDir =
    QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  const QString path = QFileDialog::getOpenFileName(
    this, tr("Import All Settings"), defaultDir, tr("JSON Files (*.json)"));
  if (path.isEmpty()) return;

  if (!confirm(this, tr("Import Settings"),
               tr("This will replace ALL current settings (bookmarks, custom "
                  "commands, color schemes, viewer options, paths, ...) with "
                  "the file's content. Any unsaved changes in this dialog "
                  "will be lost. Continue?"),
               /*defaultYes=*/false)) {
    return;
  }

  QString err;
  if (!Settings::instance().importFrom(path, &err)) {
    warn(this, tr("Import Settings"), err);
    return;
  }
  inform(this, tr("Import Settings"),
         tr("Settings imported. Close and reopen the Settings dialog to see "
            "the updated values in all tabs."));
}

} // namespace Farman
