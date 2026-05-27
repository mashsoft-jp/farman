#include "ExternalAppsTab.h"
#include "settings/Settings.h"
#include "settings/PresetIO.h"
#include "core/UserCommand.h"
#include "core/UserCommandManager.h"
#include "core/AppPresets.h"
#include "utils/Dialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyle>
#include <QToolButton>
#include <QKeyEvent>
#include <QUuid>
#include <QVBoxLayout>

namespace Farman {

namespace {

// Browse 用の小さな QToolButton (フォルダアイコン) に共通設定を 1 箇所で当てる。
//   - 強制 StrongFocus (macOS 既定では Tab 巡回に含まれないため)
//   - QSS でフォーカス時にハイライト枠を出す (native QToolButton には visible
//     focus ring が出ないため、ユーザーが今どこにフォーカスがあるか分かるように)
//   - Enter / Return でも click が走るよう、event filter を installEventFilter
//     で targetForEnter (= ExternalAppsTab) に向ける
QToolButton* makeBrowseButton(QWidget* parent, QObject* targetForEnter) {
  QToolButton* btn = new QToolButton(parent);
  btn->setIcon(parent->style()->standardIcon(QStyle::SP_DirOpenIcon));
  btn->setToolTip(QObject::tr("Browse for executable..."));
  btn->setFocusPolicy(Qt::StrongFocus);
  btn->setStyleSheet(QStringLiteral(
    "QToolButton { padding: 2px; border: 1px solid transparent; "
                  "border-radius: 3px; }"
    "QToolButton:focus { border: 2px solid palette(highlight); padding: 1px; }"));
  btn->installEventFilter(targetForEnter);
  return btn;
}

} // anonymous namespace


ExternalAppsTab::ExternalAppsTab(QWidget* parent)
  : QWidget(parent) {
  setupUi();
  loadSettings();
}

void ExternalAppsTab::setupUi() {
  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // 「Terminal」「Text Editor」を 2 ペインの GroupBox で並べる。
  // 各グループの先頭に「Preset」コンボを置き、選ぶと Program / Arguments が
  // 自動で埋まる。フィールドを手で書き換えると "(Custom)" に戻る。
  // 作業ディレクトリは常にアクティブペインの cwd に固定 (UI から削除)。
  auto buildExtAppBox = [this](
      const QString& title,
      QComboBox*&    presetCombo,
      QLineEdit*&    programEdit,
      QToolButton*&  programBrowse,
      QLineEdit*&    argsEdit,
      QPushButton*&  testButton,
      const QString& argsPlaceholder) -> QGroupBox* {
    QGroupBox* box = new QGroupBox(title, this);
    QFormLayout* form = new QFormLayout(box);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    // Preset コンボ (一番上)
    presetCombo = new QComboBox(this);
    presetCombo->setToolTip(tr(
      "Pick a detected installed application to fill Program / Arguments "
      "automatically. Editing any field below switches back to (Custom)."));
    form->addRow(tr("Preset:"), presetCombo);

    // Program (LineEdit + Browse)
    QWidget* programRow = new QWidget(this);
    QHBoxLayout* programRowLayout = new QHBoxLayout(programRow);
    programRowLayout->setContentsMargins(0, 0, 0, 0);
    programEdit = new QLineEdit(this);
    programEdit->setPlaceholderText(tr("/path/to/executable"));
    programBrowse = makeBrowseButton(this, this);
    programRowLayout->addWidget(programEdit, 1);
    programRowLayout->addWidget(programBrowse);
    form->addRow(tr("Program:"), programRow);

    // Arguments
    argsEdit = new QLineEdit(this);
    argsEdit->setPlaceholderText(argsPlaceholder);
    argsEdit->setToolTip(tr(
      "Space-separated arguments. Use double quotes for arguments with spaces. "
      "Placeholders: {dir} {otherDir} {file} {files} {name} {ext}"));
    form->addRow(tr("Arguments:"), argsEdit);

    // Test launch
    testButton = new QPushButton(tr("Test launch"), this);
    QHBoxLayout* testRow = new QHBoxLayout();
    testRow->setContentsMargins(0, 0, 0, 0);
    testRow->addStretch(1);
    testRow->addWidget(testButton);
    form->addRow(QString(), testRow);

    // 明示 Tab 順: Program → 📁 → Arguments → Test launch
    QWidget::setTabOrder(programEdit,   programBrowse);
    QWidget::setTabOrder(programBrowse, argsEdit);
    QWidget::setTabOrder(argsEdit,      testButton);

    return box;
  };

  mainLayout->addWidget(buildExtAppBox(
    tr("Terminal"),
    m_terminalPresetCombo,
    m_terminalProgramEdit, m_terminalProgramBrowse,
    m_terminalArgsEdit, m_terminalTestButton,
    tr("e.g. --working-directory={dir}")));

  mainLayout->addWidget(buildExtAppBox(
    tr("Text Editor"),
    m_editorPresetCombo,
    m_editorProgramEdit, m_editorProgramBrowse,
    m_editorArgsEdit, m_editorTestButton,
    tr("e.g. {file}")));

  // 検出 + コンボ初期化。検出は同期で軽量 (10〜20 個のパス確認のみ) なので
  // 構築フローに組み込んで問題ない。
  m_terminalPresets = detectInstalledPresets(QStringLiteral("terminal"));
  m_editorPresets   = detectInstalledPresets(QStringLiteral("editor"));
  populatePresetCombo(m_terminalPresetCombo, m_terminalPresets);
  populatePresetCombo(m_editorPresetCombo,   m_editorPresets);

  connect(m_terminalProgramBrowse, &QToolButton::clicked,
          this, &ExternalAppsTab::onBrowseTerminalProgram);
  connect(m_editorProgramBrowse, &QToolButton::clicked,
          this, &ExternalAppsTab::onBrowseEditorProgram);
  connect(m_terminalTestButton, &QPushButton::clicked,
          this, &ExternalAppsTab::onTestLaunchTerminal);
  connect(m_editorTestButton, &QPushButton::clicked,
          this, &ExternalAppsTab::onTestLaunchEditor);

  // Preset 選択 → フィールドを書き換える
  connect(m_terminalPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ExternalAppsTab::onTerminalPresetChanged);
  connect(m_editorPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ExternalAppsTab::onEditorPresetChanged);

  // フィールドを手で書き換え → Preset コンボを "(Custom)" に戻す。
  // textEdited はユーザーの入力でのみ発火し、setText() による反映では発火しない
  // ため、プリセット選択時の自動書き換えと衝突しない。
  connect(m_terminalProgramEdit, &QLineEdit::textEdited,
          this, &ExternalAppsTab::switchTerminalToCustom);
  connect(m_terminalArgsEdit,    &QLineEdit::textEdited,
          this, &ExternalAppsTab::switchTerminalToCustom);
  connect(m_editorProgramEdit,   &QLineEdit::textEdited,
          this, &ExternalAppsTab::switchEditorToCustom);
  connect(m_editorArgsEdit,      &QLineEdit::textEdited,
          this, &ExternalAppsTab::switchEditorToCustom);

  // ─── ユーザー定義コマンド (terminal / editor 以外) の編集 UI ───
  // 縦に「既存コマンドの編集フォーム × N」「新規コマンド追加フォーム」を直接
  // 並べる (外側の「Custom User Commands」 GroupBox は省略)。各既存コマンド
  // のフォームは「↑ ↓ Test launch / Update / Delete」、追加フォームは
  // 「Test launch / Add」をボタン行に持つ。
  // 既存コマンド行を縦に並べるレイアウト。buildExistingCommandRow() で
  // ここに QGroupBox を 1 つずつ addWidget する。
  m_customRowsLayout = new QVBoxLayout();
  m_customRowsLayout->setSpacing(8);
  mainLayout->addLayout(m_customRowsLayout);

  // ── 新規コマンド追加フォーム ──
  m_addCmdBox = new QGroupBox(tr("Add New Command"), this);
  QFormLayout* addForm = new QFormLayout(m_addCmdBox);
  addForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  m_addCmdName = new QLineEdit(m_addCmdBox);
  m_addCmdName->setPlaceholderText(tr("Display name (shown in Tools menu)"));
  addForm->addRow(tr("Name:"), m_addCmdName);

  QWidget* addProgramRow = new QWidget(m_addCmdBox);
  QHBoxLayout* addProgramRowLayout = new QHBoxLayout(addProgramRow);
  addProgramRowLayout->setContentsMargins(0, 0, 0, 0);
  m_addCmdProgram = new QLineEdit(m_addCmdBox);
  m_addCmdProgram->setPlaceholderText(tr("/path/to/executable"));
  m_addCmdProgramBrowse = makeBrowseButton(m_addCmdBox, this);
  addProgramRowLayout->addWidget(m_addCmdProgram, 1);
  addProgramRowLayout->addWidget(m_addCmdProgramBrowse);
  addForm->addRow(tr("Program:"), addProgramRow);

  m_addCmdArgs = new QLineEdit(m_addCmdBox);
  m_addCmdArgs->setPlaceholderText(tr("e.g. {file}  /  --flag {dir}"));
  m_addCmdArgs->setToolTip(tr(
    "Space-separated arguments. Use double quotes for arguments with spaces. "
    "Placeholders: {dir} {otherDir} {file} {files} {name} {ext}"));
  addForm->addRow(tr("Arguments:"), m_addCmdArgs);

  m_addCmdShowInTools = new QCheckBox(tr("Show in Tools menu"), m_addCmdBox);
  m_addCmdShowInTools->setChecked(true);
  addForm->addRow(QString(), m_addCmdShowInTools);

  // ボタン行: [stretch] [Test launch] [Add]
  m_addCmdTestButton = new QPushButton(tr("Test launch"), m_addCmdBox);
  m_addCmdAddButton  = new QPushButton(tr("Add"),         m_addCmdBox);
  QHBoxLayout* addBtnRow = new QHBoxLayout();
  addBtnRow->setContentsMargins(0, 0, 0, 0);
  addBtnRow->addStretch(1);
  addBtnRow->addWidget(m_addCmdTestButton);
  addBtnRow->addWidget(m_addCmdAddButton);
  addForm->addRow(QString(), addBtnRow);

  // 明示 Tab 順: Name → Program → 📁 → Arguments → Show → Test → Add
  QWidget::setTabOrder(m_addCmdName,         m_addCmdProgram);
  QWidget::setTabOrder(m_addCmdProgram,      m_addCmdProgramBrowse);
  QWidget::setTabOrder(m_addCmdProgramBrowse,m_addCmdArgs);
  QWidget::setTabOrder(m_addCmdArgs,         m_addCmdShowInTools);
  QWidget::setTabOrder(m_addCmdShowInTools,  m_addCmdTestButton);
  QWidget::setTabOrder(m_addCmdTestButton,   m_addCmdAddButton);

  mainLayout->addWidget(m_addCmdBox);

  connect(m_addCmdProgramBrowse, &QToolButton::clicked,
          this, &ExternalAppsTab::onAddCommandBrowseClicked);
  connect(m_addCmdTestButton, &QPushButton::clicked,
          this, &ExternalAppsTab::onAddCommandTestClicked);
  connect(m_addCmdAddButton,  &QPushButton::clicked,
          this, &ExternalAppsTab::onAddCommandClicked);

  // ─── 末尾: Import / Export ボタン (組み込み 2 件は除外) ───
  QHBoxLayout* ioRow = new QHBoxLayout();
  ioRow->addStretch(1);

  QPushButton* exportButton = new QPushButton(tr("Export..."), this);
  exportButton->setToolTip(tr(
    "Save user-defined commands to a JSON file. "
    "The built-in Terminal and Text Editor entries are not included."));
  connect(exportButton, &QPushButton::clicked, this, &ExternalAppsTab::onExportCommands);
  ioRow->addWidget(exportButton);

  QPushButton* importButton = new QPushButton(tr("Import..."), this);
  importButton->setToolTip(tr(
    "Load user-defined commands from a JSON file. "
    "The built-in Terminal and Text Editor entries are not affected."));
  connect(importButton, &QPushButton::clicked, this, &ExternalAppsTab::onImportCommands);
  ioRow->addWidget(importButton);

  mainLayout->addLayout(ioRow);

  mainLayout->addStretch();
}

void ExternalAppsTab::populatePresetCombo(QComboBox* combo,
                                          const QList<AppPreset>& presets) {
  combo->clear();
  // 1 行目は常に "(Custom)"。userData は空文字。
  combo->addItem(tr("(Custom)"), QString());
  for (const AppPreset& p : presets) {
    combo->addItem(p.displayName, p.id);
  }
}

void ExternalAppsTab::loadSettings() {
  auto& settings = Settings::instance();

  m_nonBuiltinUserCommands.clear();
  const QList<UserCommand> all = settings.userCommands();

  auto fillExtApp = [](const UserCommand& c,
                       QLineEdit* program, QLineEdit* args) {
    program->setText(c.program);
    args->setText(joinArgsTemplate(c.argsTemplate));
    // workingDirTemplate は UI に持たない (常にアクティブペイン)
  };

  bool gotTerm = false, gotEdit = false;
  for (const UserCommand& c : all) {
    if (c.builtin && c.builtinKind == QLatin1String("terminal") && !gotTerm) {
      fillExtApp(c, m_terminalProgramEdit, m_terminalArgsEdit);
      gotTerm = true;
    } else if (c.builtin && c.builtinKind == QLatin1String("editor") && !gotEdit) {
      fillExtApp(c, m_editorProgramEdit, m_editorArgsEdit);
      gotEdit = true;
    } else {
      m_nonBuiltinUserCommands.append(c);
    }
  }

  // 何らかの理由で builtin 2 件のいずれかが欠けている場合 (ユーザーが
  // settings.json を手で削った等) はプラットフォーム既定で UI を埋め直す。
  // これにより save 時にも 2 件が必ず復元される。
  if (!gotTerm || !gotEdit) {
    const QList<UserCommand> defaults = defaultBuiltinUserCommands();
    for (const UserCommand& d : defaults) {
      if (!gotTerm && d.builtinKind == QLatin1String("terminal")) {
        fillExtApp(d, m_terminalProgramEdit, m_terminalArgsEdit);
        gotTerm = true;
      } else if (!gotEdit && d.builtinKind == QLatin1String("editor")) {
        fillExtApp(d, m_editorProgramEdit, m_editorArgsEdit);
        gotEdit = true;
      }
    }
  }

  // フィールドが揃ったところで Preset コンボの初期選択を決める。
  // 現在値が検出済プリセットのいずれかと完全一致すればそれを、しなければ
  // "(Custom)" を選ぶ。setCurrentIndex() の発火で onTerminalPresetChanged が
  // 呼ばれるが、(a) 値は既に同じなので setText が走っても変化はなく、
  // (b) "(Custom)" 選択ロジックは何もしないので副作用はない。
  auto selectMatchedPreset = [](QComboBox* combo, const QList<AppPreset>& presets,
                                QLineEdit* program, QLineEdit* args) {
    const QString matchedId = matchPresetId(presets,
                                            program->text().trimmed(),
                                            splitArgsTemplate(args->text()));
    int index = 0; // "(Custom)"
    for (int i = 1; i < combo->count(); ++i) {
      if (combo->itemData(i).toString() == matchedId) { index = i; break; }
    }
    QSignalBlocker blocker(combo);
    combo->setCurrentIndex(index);
  };
  selectMatchedPreset(m_terminalPresetCombo, m_terminalPresets,
                      m_terminalProgramEdit, m_terminalArgsEdit);
  selectMatchedPreset(m_editorPresetCombo, m_editorPresets,
                      m_editorProgramEdit, m_editorArgsEdit);

  // ユーザー定義コマンド一覧を一括構築 (初期選択は先頭、なければ何も選ばない)。
  rebuildCustomCommandRows();
}

void ExternalAppsTab::save() {
  auto& settings = Settings::instance();

  auto buildBuiltin = [](const QString& kind, const QString& name,
                         const QLineEdit* program, const QLineEdit* args) {
    UserCommand c;
    c.id                  = QStringLiteral("user.cmd.") + kind;
    c.name                = name;
    c.builtin             = true;
    c.builtinKind         = kind;
    c.showInToolsMenu     = true;
    c.program             = program->text().trimmed();
    c.argsTemplate        = splitArgsTemplate(args->text());
    // 作業ディレクトリは常にアクティブペイン (= empty)。
    c.workingDirTemplate  = QString();
    return c;
  };

  // 既存コマンド行のウィジェット値を最終確定 (「更新」ボタン未押下でも
  // OK 押下時の最新ウィジェット値を保存に反映するため)。
  flushCustomCommandRowsToModel();

  QList<UserCommand> updated;
  updated.append(buildBuiltin(QStringLiteral("terminal"), tr("Terminal"),
                              m_terminalProgramEdit, m_terminalArgsEdit));
  updated.append(buildBuiltin(QStringLiteral("editor"), tr("Text Editor"),
                              m_editorProgramEdit, m_editorArgsEdit));
  // ユーザー定義コマンドを後ろに積む。
  for (const UserCommand& c : m_nonBuiltinUserCommands) updated.append(c);
  settings.setUserCommands(updated);
}

namespace {

// Browse 初期ディレクトリ: OS ごとに「アプリが置いてある定番フォルダ」を返す。
//   macOS:   /Applications  (見つからなければ ~)
//   Windows: %ProgramFiles% → %ProgramW6432% → C:/Program Files の順で存在確認
//   Linux:   /usr/bin       (見つからなければ ~)
QString defaultProgramStartDir() {
#if defined(Q_OS_MACOS)
  if (QDir(QStringLiteral("/Applications")).exists()) return QStringLiteral("/Applications");
#elif defined(Q_OS_WIN)
  for (const char* var : { "ProgramW6432", "ProgramFiles", "ProgramFiles(x86)" }) {
    const QByteArray v = qgetenv(var);
    if (!v.isEmpty()) {
      const QString p = QString::fromLocal8Bit(v);
      if (QDir(p).exists()) return p;
    }
  }
  if (QDir(QStringLiteral("C:/Program Files")).exists())
    return QStringLiteral("C:/Program Files");
#else  // Linux / Unix
  if (QDir(QStringLiteral("/usr/bin")).exists()) return QStringLiteral("/usr/bin");
#endif
  return QDir::homePath();
}

void browseProgramInto(QWidget* parent, QLineEdit* target, const QString& title) {
  QString start = target->text().trimmed();
  if (!start.isEmpty() && QFileInfo(start).exists()) {
    // 既に入力済 → そのファイルのあるディレクトリから始める
    const QFileInfo fi(start);
    start = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
  } else {
    start = defaultProgramStartDir();
  }
  const QString selected = QFileDialog::getOpenFileName(parent, title, start);
  if (!selected.isEmpty()) {
    // macOS の native QFileDialog で .app バンドルを選ぶと
    // "/Applications/Foo.app/" のように末尾に "/" が付くことがある。
    // この状態だと QProcess::startDetached が起動できないので除去する。
    // Windows でも "C:\App\" のような末尾区切りを念のため除去。
    QString cleaned = selected;
    while (cleaned.size() > 1 &&
           (cleaned.endsWith(QLatin1Char('/')) ||
            cleaned.endsWith(QLatin1Char('\\')))) {
      cleaned.chop(1);
    }
    target->setText(cleaned);
  }
}

UserCommand buildTransient(const QString& kind, const QString& name,
                           QLineEdit* programEdit, QLineEdit* argsEdit) {
  UserCommand c;
  c.id                 = QStringLiteral("user.cmd.") + kind;
  c.name               = name;
  c.builtin            = true;
  c.builtinKind        = kind;
  c.program            = programEdit->text().trimmed();
  c.argsTemplate       = splitArgsTemplate(argsEdit->text());
  // 作業ディレクトリはアクティブペインに固定 (= empty)
  c.workingDirTemplate = QString();
  return c;
}

} // anonymous namespace

void ExternalAppsTab::onBrowseTerminalProgram() {
  browseProgramInto(this, m_terminalProgramEdit, tr("Choose terminal executable"));
}

void ExternalAppsTab::onBrowseEditorProgram() {
  browseProgramInto(this, m_editorProgramEdit, tr("Choose editor executable"));
}

void ExternalAppsTab::onTestLaunchTerminal() {
  const UserCommand c = buildTransient(
    QStringLiteral("terminal"), tr("Terminal"),
    m_terminalProgramEdit, m_terminalArgsEdit);
  QString err;
  if (!UserCommandManager::instance().runTransient(c, &err)) {
    warn(this, tr("Test launch failed"), err);
  }
}

void ExternalAppsTab::onTestLaunchEditor() {
  const UserCommand c = buildTransient(
    QStringLiteral("editor"), tr("Text Editor"),
    m_editorProgramEdit, m_editorArgsEdit);
  QString err;
  if (!UserCommandManager::instance().runTransient(c, &err)) {
    warn(this, tr("Test launch failed"), err);
  }
}

namespace {

// 共通: 選択された Preset のフィールドを LineEdit 群に流し込む。
// "(Custom)" (= userData 空) なら何もしない。setText() は textEdited を発火
// しないので、switch*ToCustom() に取られて Custom に戻る心配はない。
// workingDirTemplate は UI から削除したためここでも反映しない (実行時は
// アクティブペインの cwd が使われる)。
void applyPresetTo(QComboBox* combo,
                   const QList<AppPreset>& presets,
                   QLineEdit* program, QLineEdit* args) {
  const QString id = combo->currentData().toString();
  if (id.isEmpty()) return;  // (Custom)
  for (const AppPreset& p : presets) {
    if (p.id == id) {
      program->setText(p.program);
      args->setText(joinArgsTemplate(p.argsTemplate));
      return;
    }
  }
}

} // anonymous namespace

void ExternalAppsTab::onTerminalPresetChanged(int /*index*/) {
  applyPresetTo(m_terminalPresetCombo, m_terminalPresets,
                m_terminalProgramEdit, m_terminalArgsEdit);
}

void ExternalAppsTab::onEditorPresetChanged(int /*index*/) {
  applyPresetTo(m_editorPresetCombo, m_editorPresets,
                m_editorProgramEdit, m_editorArgsEdit);
}

void ExternalAppsTab::switchTerminalToCustom() {
  // 既に "(Custom)" (index 0) なら何もしない。setCurrentIndex(0) を毎回呼ぶと
  // currentIndexChanged が連発するので、既に 0 のときはスキップ。
  if (m_terminalPresetCombo->currentIndex() == 0) return;
  QSignalBlocker blocker(m_terminalPresetCombo);
  m_terminalPresetCombo->setCurrentIndex(0);
}

void ExternalAppsTab::switchEditorToCustom() {
  if (m_editorPresetCombo->currentIndex() == 0) return;
  QSignalBlocker blocker(m_editorPresetCombo);
  m_editorPresetCombo->setCurrentIndex(0);
}

void ExternalAppsTab::onExportCommands() {
  if (m_nonBuiltinUserCommands.isEmpty()) {
    inform(this, tr("Export Commands"),
                             tr("No custom commands to export. "
                                "(The built-in Terminal and Editor entries are not included.)"));
    return;
  }

  const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  const QString path = QFileDialog::getSaveFileName(
    this, tr("Export Commands"),
    defaultDir + QStringLiteral("/farman-commands.json"),
    tr("JSON Files (*.json)"));
  if (path.isEmpty()) return;

  if (auto r = PresetIO::exportUserCommandsToFile(path, m_nonBuiltinUserCommands); !r.ok) {
    warn(this, tr("Export Commands"), r.error);
  }
}

void ExternalAppsTab::onImportCommands() {
  const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  const QString path = QFileDialog::getOpenFileName(
    this, tr("Import Commands"), defaultDir,
    tr("JSON Files (*.json)"));
  if (path.isEmpty()) return;

  // ファイルを読み出す
  QList<UserCommand> imported;
  if (auto r = PresetIO::importUserCommandsFromFile(path, imported); !r.ok) {
    warn(this, tr("Import Commands"), r.error);
    return;
  }

  // builtin が紛れていたら除外 (組み込み terminal/editor は別 UI 管理)。
  QList<UserCommand> filtered;
  for (const UserCommand& c : imported) {
    if (!c.builtin) filtered.append(c);
  }
  if (filtered.isEmpty()) {
    inform(this, tr("Import Commands"),
                             tr("The file contains no custom (non-builtin) commands."));
    return;
  }

  // Replace / Append を選択させる
  const int choice = choose(this,
    tr("Import Commands"),
    tr("Imported %1 command(s). How should they be applied?").arg(filtered.size()),
    {
      { tr("Replace"), Qt::Key_R },
      { tr("Append"),  Qt::Key_A },
      { tr("Cancel"),  Qt::Key_X },
    },
    /*defaultIndex=*/1,   // Append
    /*cancelIndex=*/2,
    DialogIcon::Question);
  if (choice != 0 && choice != 1) return;

  if (choice == 0) {
    m_nonBuiltinUserCommands = filtered;
  } else {
    // Append: 既存の id と衝突したら数字接尾辞でリネーム (id 衝突防止)
    QSet<QString> usedIds;
    for (const UserCommand& c : m_nonBuiltinUserCommands) usedIds.insert(c.id);
    for (UserCommand c : filtered) {
      QString base = c.id;
      QString id   = base;
      int    n     = 2;
      while (usedIds.contains(id)) {
        id = QStringLiteral("%1-%2").arg(base).arg(n++);
      }
      c.id = id;
      usedIds.insert(id);
      m_nonBuiltinUserCommands.append(c);
    }
  }

  inform(this, tr("Import Commands"),
                           tr("Imported %1 custom command(s). "
                              "They will be saved when you close Settings with OK.")
                             .arg(filtered.size()));
  // 取り込んだコマンドが見えるよう一覧を再構築。
  rebuildCustomCommandRows();
  refreshReorderButtons();
}

// ── ユーザー定義コマンドの編集 UI ──

void ExternalAppsTab::rebuildCustomCommandRows() {
  // 既存の行ウィジェットをすべて削除して m_nonBuiltinUserCommands から作り直す。
  for (const CustomCommandRow& r : m_customRows) {
    if (r.box) r.box->deleteLater();
  }
  m_customRows.clear();
  for (const UserCommand& c : m_nonBuiltinUserCommands) {
    buildExistingCommandRow(c);
  }
  // buildExistingCommandRow が個別に rebuildCustomCommandTabOrder を呼ぶが、
  // 0 件のときも巻き直しておく (削除直後など)。
  rebuildCustomCommandTabOrder();
}

void ExternalAppsTab::rebuildCustomCommandTabOrder() {
  // 目的の順:
  //   ターミナル -> エディタ (= m_editorTestButton まで)
  //   既存ユーザー定義コマンド 1 つ目の Name → ... → 1 つ目の Delete
  //   2 つ目の Name → ... → 2 つ目の Delete
  //   ...
  //   m_addCmdName → m_addCmdProgram → 📁 → m_addCmdArgs →
  //   m_addCmdShowInTools → m_addCmdTestButton → m_addCmdAddButton
  //
  // setTabOrder(a, b) は a の次に b を置く (= 既存のチェーンを差し替える)。
  // 列挙して隣り合うペアごとに呼べば全体が望みの順序に巻き直る。
  QList<QWidget*> chain;
  if (m_editorTestButton) chain.append(m_editorTestButton);
  for (const CustomCommandRow& r : m_customRows) {
    if (r.nameEdit)         chain.append(r.nameEdit);
    if (r.programEdit)      chain.append(r.programEdit);
    if (r.programBrowse)    chain.append(r.programBrowse);
    if (r.argsEdit)         chain.append(r.argsEdit);
    if (r.showInToolsCheck) chain.append(r.showInToolsCheck);
    if (r.testBtn)          chain.append(r.testBtn);
    if (r.updateBtn)        chain.append(r.updateBtn);
    if (r.deleteBtn)        chain.append(r.deleteBtn);
  }
  if (m_addCmdName)          chain.append(m_addCmdName);
  if (m_addCmdProgram)       chain.append(m_addCmdProgram);
  if (m_addCmdProgramBrowse) chain.append(m_addCmdProgramBrowse);
  if (m_addCmdArgs)          chain.append(m_addCmdArgs);
  if (m_addCmdShowInTools)   chain.append(m_addCmdShowInTools);
  if (m_addCmdTestButton)    chain.append(m_addCmdTestButton);
  if (m_addCmdAddButton)     chain.append(m_addCmdAddButton);

  for (int i = 0; i + 1 < chain.size(); ++i) {
    QWidget::setTabOrder(chain[i], chain[i + 1]);
  }
}

void ExternalAppsTab::buildExistingCommandRow(const UserCommand& cmd) {
  const QString id = cmd.id;
  // タイトルは名前 (空なら "(Unnamed)")。Update 押下時に追従して書き換える。
  auto titleFor = [this](const QString& name) {
    return name.isEmpty() ? tr("(Unnamed)") : name;
  };
  QGroupBox* box = new QGroupBox(titleFor(cmd.name), this);
  QFormLayout* form = new QFormLayout(box);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  QLineEdit* nameEdit = new QLineEdit(cmd.name, box);
  nameEdit->setPlaceholderText(tr("Display name (shown in Tools menu)"));
  form->addRow(tr("Name:"), nameEdit);

  QWidget* progRow = new QWidget(box);
  QHBoxLayout* progRowLayout = new QHBoxLayout(progRow);
  progRowLayout->setContentsMargins(0, 0, 0, 0);
  QLineEdit* programEdit = new QLineEdit(cmd.program, box);
  programEdit->setPlaceholderText(tr("/path/to/executable"));
  QToolButton* programBrowse = makeBrowseButton(box, this);
  progRowLayout->addWidget(programEdit, 1);
  progRowLayout->addWidget(programBrowse);
  form->addRow(tr("Program:"), progRow);

  QLineEdit* argsEdit = new QLineEdit(joinArgsTemplate(cmd.argsTemplate), box);
  argsEdit->setPlaceholderText(tr("e.g. {file}  /  --flag {dir}"));
  argsEdit->setToolTip(tr(
    "Space-separated arguments. Use double quotes for arguments with spaces. "
    "Placeholders: {dir} {otherDir} {file} {files} {name} {ext}"));
  form->addRow(tr("Arguments:"), argsEdit);

  QCheckBox* showCheck = new QCheckBox(tr("Show in Tools menu"), box);
  showCheck->setChecked(cmd.showInToolsMenu);
  form->addRow(QString(), showCheck);

  // 並び替え (Tools メニューでの出現順 + このタブの表示順を 1 個ずつ移動)。
  QToolButton* upBtn = new QToolButton(box);
  upBtn->setText(QStringLiteral("↑"));
  upBtn->setToolTip(tr("Move this command up"));
  upBtn->setFocusPolicy(Qt::StrongFocus);
  QToolButton* downBtn = new QToolButton(box);
  downBtn->setText(QStringLiteral("↓"));
  downBtn->setToolTip(tr("Move this command down"));
  downBtn->setFocusPolicy(Qt::StrongFocus);

  QPushButton* testBtn   = new QPushButton(tr("Test launch"), box);
  QPushButton* updateBtn = new QPushButton(tr("Update"),      box);
  QPushButton* deleteBtn = new QPushButton(tr("Delete"),      box);
  QHBoxLayout* btnRow = new QHBoxLayout();
  btnRow->setContentsMargins(0, 0, 0, 0);
  btnRow->addWidget(upBtn);
  btnRow->addWidget(downBtn);
  btnRow->addStretch(1);
  btnRow->addWidget(testBtn);
  btnRow->addWidget(updateBtn);
  btnRow->addWidget(deleteBtn);
  form->addRow(QString(), btnRow);

  // Tab 順は rebuildCustomCommandTabOrder() でまとめて再構築する。

  // 追加フォームの上に挿入する。m_customRowsLayout は m_addCmdBox とは別の
  // レイアウトなので、末尾に追加 = 追加フォームの直上に挿入される。
  m_customRowsLayout->addWidget(box);

  // 配線
  connect(programBrowse, &QToolButton::clicked, this,
          [this, programEdit]() {
    browseProgramInto(this, programEdit, tr("Choose program executable"));
  });
  // Test launch: 現在のフォーム値で transient 実行 (id は元のものを使い回す)
  connect(testBtn, &QPushButton::clicked, this,
          [this, id, nameEdit, programEdit, argsEdit, showCheck]() {
    UserCommand c;
    c.id              = id;
    c.name            = nameEdit->text().trimmed();
    c.builtin         = false;
    c.program         = programEdit->text().trimmed();
    c.argsTemplate    = splitArgsTemplate(argsEdit->text());
    c.showInToolsMenu = showCheck->isChecked();
    c.workingDirTemplate = QString();
    QString err;
    if (!UserCommandManager::instance().runTransient(c, &err)) {
      warn(this, tr("Test launch failed"), err);
    }
  });
  // Update: フォーム値をモデルへ反映し、タイトルにも追従させる
  connect(updateBtn, &QPushButton::clicked, this,
          [this, id, box, titleFor, nameEdit, programEdit, argsEdit, showCheck]() {
    for (UserCommand& cm : m_nonBuiltinUserCommands) {
      if (cm.id == id) {
        cm.name            = nameEdit->text().trimmed();
        cm.program         = programEdit->text().trimmed();
        cm.argsTemplate    = splitArgsTemplate(argsEdit->text());
        cm.showInToolsMenu = showCheck->isChecked();
        cm.workingDirTemplate = QString();
        box->setTitle(titleFor(cm.name));
        break;
      }
    }
  });
  // Delete: 確認 → モデル削除 + UI 削除
  connect(deleteBtn, &QPushButton::clicked, this,
          [this, id, box, nameEdit]() {
    const QString shown = nameEdit->text().trimmed().isEmpty()
                            ? tr("(Unnamed)")
                            : nameEdit->text().trimmed();
    // confirm() を使うことで Tab フォーカス、Y/N 単押し、Enter / Esc 全部に
    // 対応する (macOS の Full Keyboard Access 設定にも依存しない)。
    if (!confirm(this, tr("Remove Command"),
                 tr("Remove user command \"%1\"?").arg(shown),
                 /*defaultYes=*/false)) {
      return;
    }
    // モデルから削除
    for (int i = 0; i < m_nonBuiltinUserCommands.size(); ++i) {
      if (m_nonBuiltinUserCommands[i].id == id) {
        m_nonBuiltinUserCommands.removeAt(i);
        break;
      }
    }
    // m_customRows からも該当行を取り除く
    for (int i = 0; i < m_customRows.size(); ++i) {
      if (m_customRows[i].id == id) {
        m_customRows.removeAt(i);
        break;
      }
    }
    box->setParent(nullptr);
    box->deleteLater();
    // Tab 順を再構築 (削除した行を抜いた状態に揃える) + 上/下ボタンの
    // enable 状態も並び替え後の両端に合わせて更新する。
    rebuildCustomCommandTabOrder();
    refreshReorderButtons();
  });

  // 並び替えボタンの接続
  connect(upBtn,   &QToolButton::clicked, this, [this, id]() { moveCommandUp(id); });
  connect(downBtn, &QToolButton::clicked, this, [this, id]() { moveCommandDown(id); });

  CustomCommandRow row;
  row.id               = id;
  row.box              = box;
  row.nameEdit         = nameEdit;
  row.programEdit      = programEdit;
  row.programBrowse    = programBrowse;
  row.argsEdit         = argsEdit;
  row.showInToolsCheck = showCheck;
  row.upBtn            = upBtn;
  row.downBtn          = downBtn;
  row.testBtn          = testBtn;
  row.updateBtn        = updateBtn;
  row.deleteBtn        = deleteBtn;
  m_customRows.append(row);

  rebuildCustomCommandTabOrder();
  refreshReorderButtons();
}

void ExternalAppsTab::moveCommandUp(const QString& id) {
  // m_customRows と m_nonBuiltinUserCommands は常に同じ並び順を保つ前提
  // (= buildExistingCommandRow の append 順)。両方を同期的に並び替える。
  int rowIdx = -1;
  for (int i = 0; i < m_customRows.size(); ++i) {
    if (m_customRows[i].id == id) { rowIdx = i; break; }
  }
  if (rowIdx <= 0) return;  // 先頭 or 見つからない

  m_customRows.swapItemsAt(rowIdx, rowIdx - 1);
  m_nonBuiltinUserCommands.swapItemsAt(rowIdx, rowIdx - 1);
  // 1 個上に取り直して再挿入する。QBoxLayout::insertWidget は同じウィジェット
  // を渡すと先に detach されるので、明示 takeAt は不要。
  m_customRowsLayout->insertWidget(rowIdx - 1, m_customRows[rowIdx - 1].box);

  rebuildCustomCommandTabOrder();
  refreshReorderButtons();
}

void ExternalAppsTab::moveCommandDown(const QString& id) {
  int rowIdx = -1;
  for (int i = 0; i < m_customRows.size(); ++i) {
    if (m_customRows[i].id == id) { rowIdx = i; break; }
  }
  if (rowIdx < 0 || rowIdx >= m_customRows.size() - 1) return;  // 末尾 or 無し

  m_customRows.swapItemsAt(rowIdx, rowIdx + 1);
  m_nonBuiltinUserCommands.swapItemsAt(rowIdx, rowIdx + 1);
  m_customRowsLayout->insertWidget(rowIdx + 1, m_customRows[rowIdx + 1].box);

  rebuildCustomCommandTabOrder();
  refreshReorderButtons();
}

void ExternalAppsTab::refreshReorderButtons() {
  const int n = m_customRows.size();
  for (int i = 0; i < n; ++i) {
    if (m_customRows[i].upBtn)   m_customRows[i].upBtn->setEnabled(i > 0);
    if (m_customRows[i].downBtn) m_customRows[i].downBtn->setEnabled(i < n - 1);
  }
}

void ExternalAppsTab::flushCustomCommandRowsToModel() {
  // 各行の現在のウィジェット内容を、対応する m_nonBuiltinUserCommands エントリ
  // へ書き戻す。Update ボタン未押下でも OK / Apply で内容が保存されるよう、
  // save() の冒頭で呼ぶ。
  for (const CustomCommandRow& r : m_customRows) {
    for (UserCommand& cm : m_nonBuiltinUserCommands) {
      if (cm.id == r.id) {
        if (r.nameEdit)         cm.name            = r.nameEdit->text().trimmed();
        if (r.programEdit)      cm.program         = r.programEdit->text().trimmed();
        if (r.argsEdit)         cm.argsTemplate    = splitArgsTemplate(r.argsEdit->text());
        if (r.showInToolsCheck) cm.showInToolsMenu = r.showInToolsCheck->isChecked();
        cm.workingDirTemplate  = QString();
        break;
      }
    }
  }
}

void ExternalAppsTab::onAddCommandBrowseClicked() {
  if (!m_addCmdProgram) return;
  browseProgramInto(this, m_addCmdProgram, tr("Choose program executable"));
}

void ExternalAppsTab::onAddCommandTestClicked() {
  if (!m_addCmdProgram) return;
  UserCommand c;
  c.id              = QStringLiteral("user.cmd.test.transient");
  c.name            = m_addCmdName->text().trimmed();
  c.builtin         = false;
  c.program         = m_addCmdProgram->text().trimmed();
  c.argsTemplate    = splitArgsTemplate(m_addCmdArgs->text());
  c.showInToolsMenu = m_addCmdShowInTools->isChecked();
  c.workingDirTemplate = QString();
  QString err;
  if (!UserCommandManager::instance().runTransient(c, &err)) {
    warn(this, tr("Test launch failed"), err);
  }
}

void ExternalAppsTab::onAddCommandClicked() {
  // Program が空ならエラー (名前は空でも追加させ、"(Unnamed)" として並ぶ)
  const QString program = m_addCmdProgram->text().trimmed();
  if (program.isEmpty()) {
    warn(this, tr("Add Command"),
                         tr("Program path is required."));
    return;
  }
  UserCommand c;
  // ID は安定 UUID。後で Keybindings タブからショートカット割当てしたとき
  // 名前を変えても ID は不変。
  c.id              = QStringLiteral("user.cmd.") +
                      QUuid::createUuid().toString(QUuid::WithoutBraces);
  c.name            = m_addCmdName->text().trimmed();
  c.builtin         = false;
  c.program         = program;
  c.argsTemplate    = splitArgsTemplate(m_addCmdArgs->text());
  c.showInToolsMenu = m_addCmdShowInTools->isChecked();
  c.workingDirTemplate = QString();

  m_nonBuiltinUserCommands.append(c);
  buildExistingCommandRow(c);

  // 追加フォームをクリア (次の入力に備えて)。
  m_addCmdName->clear();
  m_addCmdProgram->clear();
  m_addCmdArgs->clear();
  m_addCmdShowInTools->setChecked(true);
  m_addCmdName->setFocus();
}

bool ExternalAppsTab::eventFilter(QObject* watched, QEvent* event) {
  // makeBrowseButton() で installEventFilter された Browse 用 QToolButton に
  // 対し、Enter / Return キーを Space と同じ「click」扱いにする。
  if (event->type() == QEvent::KeyPress) {
    if (auto* btn = qobject_cast<QToolButton*>(watched)) {
      auto* keyEvent = static_cast<QKeyEvent*>(event);
      const int key = keyEvent->key();
      if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        btn->animateClick();  // 少し光らせつつ clicked() を発火
        return true;          // イベント消費 (デフォルトの Return 動作を抑止)
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

} // namespace Farman
