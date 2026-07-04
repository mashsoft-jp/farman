#include "AppearanceTab.h"
#include "settings/PresetIO.h"
#include "utils/Dialogs.h"
#include <QVBoxLayout>
#include <QFileDialog>
#include <QStandardPaths>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QSpinBox>
#include <QFontDialog>
#include <QColorDialog>
#include <QRadioButton>
#include <QButtonGroup>
#include <QTabWidget>
#include <QTabBar>
#include <QLineEdit>
#include <QRegularExpression>
#include <QEvent>
#include <QDebug>

namespace Farman {

AppearanceTab::AppearanceTab(QWidget* parent)
  : QWidget(parent)
  , m_fontButton(nullptr) {
  setupUi();
  loadSettings();
}

void AppearanceTab::setupUi() {
  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // ─── Theme グループ: Light / Dark / Auto モード切替 + プリセット / I/O ──
  // モードを切替えると、その瞬間のウィジェット値が「直前まで編集していた側」
  // のシャドースキームへ書き戻され、反対側の値が読み込まれる。
  // OK ボタン押下で両側のスキームと themeMode が一括コミットされる。
  // プリセット適用 / Import は「現在編集中の側」だけを上書きする
  // (Light タブ表示中は light、Dark タブ表示中は dark へ)。両側を持つ
  // プリセット (Solarized 等) は両方を一括上書きする。
  QGroupBox* themeGroup = new QGroupBox(tr("Theme"), this);
  QVBoxLayout* themeOuter = new QVBoxLayout(themeGroup);

  // 行 1: Mode (ラジオ 3 つ) + (currently applying: ...)
  QHBoxLayout* modeRow = new QHBoxLayout();
  modeRow->addWidget(new QLabel(tr("Mode:"), this));
  m_modeAutoRadio  = new QRadioButton(tr("Auto (follow OS)"), this);
  m_modeAutoRadio->setToolTip(tr(
    "Auto follows the operating system's appearance setting. "
    "Editing automatically targets the currently active side."));
  m_modeLightRadio = new QRadioButton(tr("Light"), this);
  m_modeDarkRadio  = new QRadioButton(tr("Dark"),  this);
  // 排他化 (QRadioButton 同じ親で自動グルーピングされるが、明示しておく)
  auto* modeGroup = new QButtonGroup(this);
  modeGroup->setExclusive(true);
  modeGroup->addButton(m_modeAutoRadio);
  modeGroup->addButton(m_modeLightRadio);
  modeGroup->addButton(m_modeDarkRadio);
  modeRow->addWidget(m_modeAutoRadio);
  modeRow->addWidget(m_modeLightRadio);
  modeRow->addWidget(m_modeDarkRadio);
  modeRow->addSpacing(12);

  m_autoEffectiveLabel = new QLabel(this);
  m_autoEffectiveLabel->setStyleSheet("QLabel { color: palette(mid); font-style: italic; }");
  modeRow->addWidget(m_autoEffectiveLabel);
  modeRow->addStretch();
  themeOuter->addLayout(modeRow);

  // ラジオが ON にトグルした瞬間だけ反応する (= OFF 側のシグナルは無視)
  auto modeRadioToggled = [this](bool on) { if (on) applyThemeModeChange(); };
  connect(m_modeAutoRadio,  &QRadioButton::toggled, this, modeRadioToggled);
  connect(m_modeLightRadio, &QRadioButton::toggled, this, modeRadioToggled);
  connect(m_modeDarkRadio,  &QRadioButton::toggled, this, modeRadioToggled);

  // 行 2: Preset + Apply + Export + Import
  QHBoxLayout* presetRow = new QHBoxLayout();
  presetRow->addWidget(new QLabel(tr("Preset:"), this));
  m_themePresetCombo = new QComboBox(this);
  m_themePresetCombo->setMinimumWidth(200);
  m_themePresetCombo->setToolTip(tr(
    "Pick a bundled theme to load its colors into the current side. "
    "Press OK to commit or Cancel to revert."));
  // 中身は loadSettings → rebuildPresetCombo() で kind フィルタして埋める。
  // currentIndexChanged で即時プレビュー反映 (Apply ボタンは廃止)。
  connect(m_themePresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) { onApplyThemePreset(); });
  presetRow->addWidget(m_themePresetCombo);

  presetRow->addSpacing(16);
  m_themeExportButton = new QPushButton(tr("Export..."), this);
  m_themeExportButton->setToolTip(tr("Save both Light and Dark schemes to a JSON file"));
  connect(m_themeExportButton, &QPushButton::clicked, this, &AppearanceTab::onExportTheme);
  presetRow->addWidget(m_themeExportButton);

  m_themeImportButton = new QPushButton(tr("Import..."), this);
  m_themeImportButton->setToolTip(
    tr("Load a theme JSON file. Single-side files update only the matching side."));
  connect(m_themeImportButton, &QPushButton::clicked, this, &AppearanceTab::onImportTheme);
  presetRow->addWidget(m_themeImportButton);

  presetRow->addStretch();
  themeOuter->addLayout(presetRow);

  mainLayout->addWidget(themeGroup);

  // ファイルリスト外観 (ベース色 / UI・リストフォント / カテゴリ色 / アドレス /
  // カーソル / ディレクトリ比較色) を直接下に置く。
  // ビュアー (テキスト / バイナリ / 画像) のフォント / 配色 / 透過は、各ビュアーの
  // プラグイン設定 (Settings → Plugins → 詳細 → 設定...) へ移設した。テーマ非依存
  // のビュアー挙動設定 (拡張子/MIME/エンコーディング/zoom 等) も同様。
  mainLayout->addWidget(buildMainPage(), /*stretch*/ 1);
}

QWidget* AppearanceTab::buildMainPage() {
  QWidget* page = new QWidget(this);
  QVBoxLayout* mainLayout = new QVBoxLayout(page);

  // 色ボタン生成のヘルパー
  auto makeColorButton = [this](QColor& storedValue, const QString& dialogTitle) -> QPushButton* {
    QPushButton* btn = new QPushButton(this);
    btn->setFixedWidth(100);
    connect(btn, &QPushButton::clicked, this, [this, &storedValue, btn, dialogTitle]() {
      QColor picked = QColorDialog::getColor(
        storedValue.isValid() ? storedValue : QColor(Qt::black),
        this, dialogTitle, QColorDialog::ShowAlphaChannel);
      if (picked.isValid()) {
        storedValue = picked;
        updateColorButton(btn, picked);
      }
    });
    return btn;
  };

  // ラベル + ウィジェットを横並びの 1 ペアにまとめるヘルパー
  auto addPair = [this](QHBoxLayout* row, const QString& labelText, QWidget* w) {
    QLabel* label = new QLabel(labelText, this);
    row->addWidget(label);
    row->addWidget(w);
    row->addSpacing(12);
  };

  // ─── 全般 UI グループ: 個別設定外の地色 + 文字色 + UI フォント ─────
  // QPalette の Window / Base / Mid / Button / ... 等、個別設定で覆われない
  // 部分は applyThemeFields() でここから線形補間して算出される。
  QGroupBox* baseGroup = new QGroupBox(tr("UI Overall"), this);
  baseGroup->setToolTip(tr(
    "Default font and background/text colors for dialogs, menus, buttons, "
    "tooltips and any other UI elements not individually themed below."));
  QHBoxLayout* baseRow = new QHBoxLayout(baseGroup);

  m_uiFontButton = new QPushButton(tr("Select Font..."), this);
  m_uiFontButton->setToolTip(tr(
    "Default font for dialogs, menus, buttons and other generic widgets. "
    "File list / Address / Viewer fonts are configured separately."));
  connect(m_uiFontButton, &QPushButton::clicked, this, [this]() {
    bool ok = false;
    const QFont chosen = QFontDialog::getFont(&ok, m_uiFontValue, this, tr("UI Font"));
    if (ok) {
      m_uiFontValue = chosen;
      m_uiFontButton->setText(QString("%1, %2pt")
        .arg(m_uiFontValue.family())
        .arg(m_uiFontValue.pointSize()));
    }
  });
  addPair(baseRow, tr("Font:"), m_uiFontButton);

  m_baseFgButton = makeColorButton(m_baseFgValue, tr("Base Foreground Color"));
  m_baseBgButton = makeColorButton(m_baseBgValue, tr("Base Background Color"));
  addPair(baseRow, tr("Foreground:"), m_baseFgButton);
  addPair(baseRow, tr("Background:"), m_baseBgButton);
  baseRow->addStretch();
  mainLayout->addWidget(baseGroup);

  // ─── Address グループ ───
  // 通常 FS のアドレスバーと、アーカイブ内ブラウジング時のアドレスバー色を
  // 1 つのグループに集約。
  // 1 行目: Font (共通) と Normal の色設定を並列。
  // 2 行目: Archive の色設定。Normal の列 (通常: / 文字色: / 背景色:) と
  //         縦に揃えたいので QGridLayout を 1 枚使う。
  QGroupBox* addressGroup = new QGroupBox(tr("Address"), this);
  QGridLayout* addressGrid = new QGridLayout(addressGroup);

  m_addressFontButton = new QPushButton(tr("Select Font..."), this);
  m_addressFontButton->setToolTip(tr("Choose the font for the address bar above each pane"));
  connect(m_addressFontButton, &QPushButton::clicked, this, [this]() {
    bool ok = false;
    const QFont chosen = QFontDialog::getFont(&ok, m_addressFontValue, this, tr("Address Font"));
    if (ok) {
      m_addressFontValue = chosen;
      m_addressFontButton->setText(QString("%1, %2pt")
        .arg(m_addressFontValue.family())
        .arg(m_addressFontValue.pointSize()));
    }
  });

  m_addressFgButton = makeColorButton(m_addressFgValue, tr("Address Foreground Color"));
  m_addressBgButton = makeColorButton(m_addressBgValue, tr("Address Background Color"));
  m_archiveAddressFgButton = makeColorButton(
    m_archiveAddressFgValue, tr("Archive Address Foreground Color"));
  m_archiveAddressBgButton = makeColorButton(
    m_archiveAddressBgValue, tr("Archive Address Background Color"));

  // 列構成: [0]Font: [1]Fontボタン [2]通常: / アーカイブ: [3]文字色: [4]色 [5]背景色: [6]色 [7]余白
  int addrRow = 0;
  // 1 行目: Font + Normal の色
  addressGrid->addWidget(new QLabel(tr("Font:"),       this), addrRow, 0);
  addressGrid->addWidget(m_addressFontButton,                addrRow, 1);
  addressGrid->addWidget(new QLabel(tr("Normal:"),     this), addrRow, 2);
  addressGrid->addWidget(new QLabel(tr("Foreground:"), this), addrRow, 3);
  addressGrid->addWidget(m_addressFgButton,                  addrRow, 4);
  addressGrid->addWidget(new QLabel(tr("Background:"), this), addrRow, 5);
  addressGrid->addWidget(m_addressBgButton,                  addrRow, 6);
  ++addrRow;
  // 2 行目: Archive の色 (Font 側 [0]/[1] は空)
  addressGrid->addWidget(new QLabel(tr("Archive:"),    this), addrRow, 2);
  addressGrid->addWidget(new QLabel(tr("Foreground:"), this), addrRow, 3);
  addressGrid->addWidget(m_archiveAddressFgButton,           addrRow, 4);
  addressGrid->addWidget(new QLabel(tr("Background:"), this), addrRow, 5);
  addressGrid->addWidget(m_archiveAddressBgButton,           addrRow, 6);
  addressGrid->setColumnStretch(7, 1);

  mainLayout->addWidget(addressGroup);

  // ─── Directory Compare グループ ───
  // ディレクトリ比較モード中の差分着色 (Differ / OnlyHere)。
  // Same は専用色を持たず通常のカテゴリ色を使う。
  // QGridLayout で列を共有させて、左ラベル (差分あり / このペインのみ) と、
  // 続く「文字色: ▣ 背景色: ▣」のカラムを縦に揃える。
  QGroupBox* compareGroup = new QGroupBox(tr("Directory Compare"), this);
  QGridLayout* compareGrid = new QGridLayout(compareGroup);
  m_compareDifferFgButton   = makeColorButton(m_compareDifferFgValue,   tr("Differ Foreground Color"));
  m_compareDifferBgButton   = makeColorButton(m_compareDifferBgValue,   tr("Differ Background Color"));
  m_compareOnlyHereFgButton = makeColorButton(m_compareOnlyHereFgValue, tr("Only-Here Foreground Color"));
  m_compareOnlyHereBgButton = makeColorButton(m_compareOnlyHereBgValue, tr("Only-Here Background Color"));

  // 列構成: [0]カテゴリラベル [1]文字色: [2]色ボタン [3]背景色: [4]色ボタン
  //         [5]右側余白用 stretch
  int row = 0;
  compareGrid->addWidget(new QLabel(tr("Differ:"),    this),       row, 0);
  compareGrid->addWidget(new QLabel(tr("Foreground:"), this),      row, 1);
  compareGrid->addWidget(m_compareDifferFgButton,                  row, 2);
  compareGrid->addWidget(new QLabel(tr("Background:"), this),      row, 3);
  compareGrid->addWidget(m_compareDifferBgButton,                  row, 4);
  ++row;
  compareGrid->addWidget(new QLabel(tr("Only-Here:"),  this),      row, 0);
  compareGrid->addWidget(new QLabel(tr("Foreground:"), this),      row, 1);
  compareGrid->addWidget(m_compareOnlyHereFgButton,                row, 2);
  compareGrid->addWidget(new QLabel(tr("Background:"), this),      row, 3);
  compareGrid->addWidget(m_compareOnlyHereBgButton,                row, 4);
  // 右側に余白を確保して左寄せ表示にする
  compareGrid->setColumnStretch(5, 1);

  mainLayout->addWidget(compareGroup);

  // ─── Cursor グループ: Shape / Thickness / Active / Inactive を横並び ─
  QGroupBox* cursorGroup = new QGroupBox(tr("Cursor"), this);
  QHBoxLayout* cursorRow = new QHBoxLayout(cursorGroup);

  m_cursorShapeCombo = new QComboBox(this);
  m_cursorShapeCombo->addItem(tr("Underline"),      static_cast<int>(CursorShape::Underline));
  m_cursorShapeCombo->addItem(tr("Row Background"), static_cast<int>(CursorShape::RowBackground));
  m_cursorShapeCombo->setToolTip(
    tr("Underline: thin line at the bottom of the row.\n"
       "Row Background: fill the entire row with the cursor color."));
  addPair(cursorRow, tr("Shape:"), m_cursorShapeCombo);

  m_cursorThicknessSpin = new QSpinBox(this);
  m_cursorThicknessSpin->setRange(1, 32);
  m_cursorThicknessSpin->setSuffix(tr(" px"));
  m_cursorThicknessSpin->setToolTip(tr("Underline thickness in pixels (used only when shape is Underline)"));
  addPair(cursorRow, tr("Thickness:"), m_cursorThicknessSpin);

  // Underline 以外のときは Thickness を無効化
  connect(m_cursorShapeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
    const auto shape = static_cast<CursorShape>(m_cursorShapeCombo->currentData().toInt());
    m_cursorThicknessSpin->setEnabled(shape == CursorShape::Underline);
  });

  m_cursorActiveButton   = makeColorButton(m_cursorActiveValue,   tr("Active Cursor Color"));
  m_cursorInactiveButton = makeColorButton(m_cursorInactiveValue, tr("Inactive Cursor Color"));
  addPair(cursorRow, tr("Active:"),   m_cursorActiveButton);
  addPair(cursorRow, tr("Inactive:"), m_cursorInactiveButton);
  cursorRow->addStretch();
  mainLayout->addWidget(cursorGroup);

  // サムネイルサイズはペインのフッタ View Mode popup (List / Thumbnail S/M/L)
  // で選ぶ運用に統一したため、Appearance タブには UI を出さない。

  // ─── File List グループ: フォント/表示形式 + ファイル種別カラー ──
  QGroupBox* fileListGroup = new QGroupBox(tr("File List"), this);
  QVBoxLayout* fileListOuter = new QVBoxLayout(fileListGroup);

  // Font と Row Height を横並びの 1 行に。
  // ファイルサイズ・日時の表示形式は Behavior タブの「List Display」へ移動済み。
  // ここは装飾 (フォント / 色 / Row Height / カテゴリカラー) だけ。
  QHBoxLayout* fileListRow = new QHBoxLayout();

  m_fontButton = new QPushButton(tr("Select Font..."), this);
  m_fontButton->setToolTip(tr("Choose the font for the file list"));
  connect(m_fontButton, &QPushButton::clicked, this, &AppearanceTab::onSelectFont);
  addPair(fileListRow, tr("Font:"), m_fontButton);

  // Row Height — 0 を "Auto" として扱うため specialValueText を使う。
  m_rowHeightSpin = new QSpinBox(this);
  m_rowHeightSpin->setRange(0, 200);
  m_rowHeightSpin->setSpecialValueText(tr("Auto"));
  m_rowHeightSpin->setSuffix(tr(" px"));
  m_rowHeightSpin->setToolTip(
    tr("Custom row height for the file list, in pixels. 'Auto' uses the default height."));
  fileListRow->addSpacing(16);
  addPair(fileListRow, tr("Row Height:"), m_rowHeightSpin);

  fileListRow->addStretch();
  fileListOuter->addLayout(fileListRow);

  // 下のアクティブパネルグループとの間に余白を入れる
  fileListOuter->addSpacing(8);

  // ファイル種別ごとのカラーリング (4 グリッド)
  QVBoxLayout* categoryOuter = fileListOuter;

  auto buildStateGrid = [this](const QString& title, bool selected, bool inactive) -> QGroupBox* {
    QGroupBox* box = new QGroupBox(title, this);
    QGridLayout* grid = new QGridLayout(box);
    grid->addWidget(new QLabel(tr("Category"),   this), 0, 0);
    grid->addWidget(new QLabel(tr("Foreground"), this), 0, 1);
    grid->addWidget(new QLabel(tr("Background"), this), 0, 2);
    grid->addWidget(new QLabel(tr("Bold"),       this), 0, 3);
    buildCategoryRow(grid, 1, FileCategory::Normal,    tr("Normal"),    selected, inactive);
    buildCategoryRow(grid, 2, FileCategory::Hidden,    tr("Hidden"),    selected, inactive);
    buildCategoryRow(grid, 3, FileCategory::Directory, tr("Directory"), selected, inactive);
    // 余り行で縦方向のストレッチを吸収して、ヘッダー＋3 行を上端に詰める
    grid->setRowStretch(4, 1);
    return box;
  };

  QGroupBox* activeGroup = new QGroupBox(tr("Active Pane"), this);
  QHBoxLayout* activeRow = new QHBoxLayout(activeGroup);
  activeRow->addWidget(buildStateGrid(tr("Normal State"),   false, false));
  activeRow->addWidget(buildStateGrid(tr("Selected State"), true,  false));
  categoryOuter->addWidget(activeGroup);

  // 非アクティブパネル設定:
  //   - 外枠は普通の (non-checkable, タイトル無し) QGroupBox = 単なる枠
  //   - 中の最初の行に QCheckBox "Inactive Pane" を置く。これが ON/OFF と
  //     見出しを兼ね、かつ macOS native のフォーカスリング (青いハロー) を
  //     QWidget として受け取れる
  //   - チェック OFF 時は内部の state grid 2 つを setEnabled(false) で
  //     グレーアウトさせる (旧 checkable QGroupBox の挙動を手で再現)
  m_inactivePaneGroup = new QGroupBox(this);
  QVBoxLayout* inactiveVBox = new QVBoxLayout(m_inactivePaneGroup);

  m_inactivePaneCheck = new QCheckBox(tr("Inactive Pane"), this);
  m_inactivePaneCheck->setToolTip(
    tr("Enable separate colors for the non-active pane. "
       "When off, the active colors are used for both panes."));
  inactiveVBox->addWidget(m_inactivePaneCheck);

  m_inactiveNormalGrid   = buildStateGrid(tr("Normal State"),   false, true);
  m_inactiveSelectedGrid = buildStateGrid(tr("Selected State"), true,  true);
  QHBoxLayout* inactiveRow = new QHBoxLayout();
  inactiveRow->addWidget(m_inactiveNormalGrid);
  inactiveRow->addWidget(m_inactiveSelectedGrid);
  inactiveVBox->addLayout(inactiveRow);
  categoryOuter->addWidget(m_inactivePaneGroup);

  // チェック状態に応じて内部 grid の enabled を切替える。
  connect(m_inactivePaneCheck, &QCheckBox::toggled, this, [this](bool checked) {
    if (m_inactiveNormalGrid)   m_inactiveNormalGrid->setEnabled(checked);
    if (m_inactiveSelectedGrid) m_inactiveSelectedGrid->setEnabled(checked);
  });

  mainLayout->addWidget(fileListGroup, /*stretch*/ 1);

  // Tab 順の調整: アクティブパネルの「Selected State / Directory / Bold」
  // (= 最後の太字チェック) の次が「Inactive Pane」の QCheckBox になるように
  // する。デフォルトの作成順だと、Active Selected 最終セル → Inactive Normal
  // 1 セル目に飛んでしまい、Inactive Pane の ON/OFF に辿り着くのに巻き戻し
  // 操作が必要だった。
  if (m_inactivePaneCheck) {
    const int dirIdx = static_cast<int>(FileCategory::Directory);
    auto* lastActiveCell = m_categoryRows[dirIdx].selected.boldCheck;
    auto* firstInactiveCell =
      m_categoryRows[static_cast<int>(FileCategory::Normal)].inactiveNormal.fgButton;
    if (lastActiveCell && firstInactiveCell) {
      setTabOrder(lastActiveCell,    m_inactivePaneCheck);
      setTabOrder(m_inactivePaneCheck, firstInactiveCell);
    }
  }

  return page;
}

void AppearanceTab::loadSettings() {
  auto& settings = Settings::instance();

  // ── テーマスキーム: Light/Dark 双方をシャドーバッファに取り込む ──
  m_dialogLight = settings.scheme(ThemeMode::Light);
  m_dialogDark  = settings.scheme(ThemeMode::Dark);
  m_dialogMode  = settings.themeMode();
  // 編集対象 (= 現在 effective なテーマ)
  // Auto のときは OS を直接判定 (settings.effectiveTheme() は Settings 側の
  // m_themeMode に依存するので、ダイアログ内で Mode を Auto に変えた直後だと
  // まだ古いモードを参照してしまう)。
  m_dialogEditingSide = (m_dialogMode == ThemeMode::Auto)
                          ? settings.detectOsTheme()
                          : m_dialogMode;

  // モードラジオを反映 (signal を抑止して再ロードを防ぐ)
  {
    QSignalBlocker b1(m_modeAutoRadio);
    QSignalBlocker b2(m_modeLightRadio);
    QSignalBlocker b3(m_modeDarkRadio);
    switch (m_dialogMode) {
      case ThemeMode::Auto:  m_modeAutoRadio->setChecked(true);  break;
      case ThemeMode::Light: m_modeLightRadio->setChecked(true); break;
      case ThemeMode::Dark:  m_modeDarkRadio->setChecked(true);  break;
    }
  }

  // 編集対象側のスキームをウィジェットに流し込む
  loadFromScheme(currentScheme());
  // プリセットコンボを kind フィルタ済みで再構築
  rebuildPresetCombo();
  updateAutoEffectiveLabel();

  // 非アクティブペイン設定 (テーマ非依存; グローバル設定)
  // QCheckBox の setChecked が toggled シグナルを発火しない場合 (= 値が既に
  // 一致) に備え、内部 grid の enable 状態は明示的にも反映しておく。
  const bool useInactive = settings.useInactivePaneColors();
  m_inactivePaneCheck->setChecked(useInactive);
  if (m_inactiveNormalGrid)   m_inactiveNormalGrid->setEnabled(useInactive);
  if (m_inactiveSelectedGrid) m_inactiveSelectedGrid->setEnabled(useInactive);

  // カーソル形状 / 太さ (テーマ非依存)
  for (int i = 0; i < m_cursorShapeCombo->count(); ++i) {
    if (m_cursorShapeCombo->itemData(i).toInt() == static_cast<int>(settings.cursorShape())) {
      m_cursorShapeCombo->setCurrentIndex(i);
      break;
    }
  }
  m_cursorThicknessSpin->setValue(settings.cursorThickness());
  m_cursorThicknessSpin->setEnabled(settings.cursorShape() == CursorShape::Underline);

  // ビュアー設定 (拡張子 / MIME / エンコーディング / ズーム / フォント / 配色 /
  // 透過) は各ビュアーのプラグイン設定へ移設済み。外観タブは扱わない。
}

ColorScheme& AppearanceTab::currentScheme() {
  return m_dialogEditingSide == ThemeMode::Dark ? m_dialogDark : m_dialogLight;
}

void AppearanceTab::loadFromScheme(const ColorScheme& s) {
  // ベース色 + UI フォント
  m_baseFgValue = s.baseForeground;
  m_baseBgValue = s.baseBackground;
  if (m_baseFgButton) updateColorButton(m_baseFgButton, m_baseFgValue);
  if (m_baseBgButton) updateColorButton(m_baseBgButton, m_baseBgValue);
  m_uiFontValue = s.uiFont;
  if (m_uiFontButton) {
    m_uiFontButton->setText(QString("%1, %2pt")
      .arg(m_uiFontValue.family()).arg(m_uiFontValue.pointSize()));
  }

  // フォント
  m_selectedFont = s.listFont;
  m_fontButton->setText(QString("%1, %2pt").arg(m_selectedFont.family()).arg(m_selectedFont.pointSize()));
  m_addressFontValue = s.addressFont;
  m_addressFontButton->setText(QString("%1, %2pt").arg(m_addressFontValue.family()).arg(m_addressFontValue.pointSize()));

  // 行高
  m_rowHeightSpin->setValue(s.fileListRowHeight);

  // ファイル種別カラー
  for (int i = 0; i < static_cast<int>(FileCategory::Count); ++i) {
    auto setState = [](CategoryStateRow& r, const CategoryColor& v, AppearanceTab* self) {
      r.value = v;
      self->updateColorButton(r.fgButton, v.foreground);
      self->updateColorButton(r.bgButton, v.background);
      r.boldCheck->setChecked(v.bold);
    };
    setState(m_categoryRows[i].normal,           s.categoryColors[i],                 this);
    setState(m_categoryRows[i].selected,         s.selectedCategoryColors[i],         this);
    setState(m_categoryRows[i].inactiveNormal,   s.inactiveCategoryColors[i],         this);
    setState(m_categoryRows[i].inactiveSelected, s.inactiveSelectedCategoryColors[i], this);
  }

  // アドレス / カーソル色
  m_addressFgValue = s.addressForeground;
  m_addressBgValue = s.addressBackground;
  updateColorButton(m_addressFgButton, m_addressFgValue);
  updateColorButton(m_addressBgButton, m_addressBgValue);
  m_archiveAddressFgValue = s.archiveAddressForeground;
  m_archiveAddressBgValue = s.archiveAddressBackground;
  updateColorButton(m_archiveAddressFgButton, m_archiveAddressFgValue);
  updateColorButton(m_archiveAddressBgButton, m_archiveAddressBgValue);
  m_compareDifferFgValue   = s.compareDifferForeground;
  m_compareDifferBgValue   = s.compareDifferBackground;
  m_compareOnlyHereFgValue = s.compareOnlyHereForeground;
  m_compareOnlyHereBgValue = s.compareOnlyHereBackground;
  updateColorButton(m_compareDifferFgButton,   m_compareDifferFgValue);
  updateColorButton(m_compareDifferBgButton,   m_compareDifferBgValue);
  updateColorButton(m_compareOnlyHereFgButton, m_compareOnlyHereFgValue);
  updateColorButton(m_compareOnlyHereBgButton, m_compareOnlyHereBgValue);

  m_cursorActiveValue   = s.cursorActiveColor;
  m_cursorInactiveValue = s.cursorInactiveColor;
  updateColorButton(m_cursorActiveButton,   m_cursorActiveValue);
  updateColorButton(m_cursorInactiveButton, m_cursorInactiveValue);
}

void AppearanceTab::saveToScheme(ColorScheme& s) const {
  s.baseBackground     = m_baseBgValue;
  s.baseForeground     = m_baseFgValue;
  s.uiFont             = m_uiFontValue;
  s.listFont           = m_selectedFont;
  s.addressFont        = m_addressFontValue;
  s.fileListRowHeight  = m_rowHeightSpin->value();

  for (int i = 0; i < static_cast<int>(FileCategory::Count); ++i) {
    s.categoryColors[i]                  = m_categoryRows[i].normal.value;
    s.selectedCategoryColors[i]          = m_categoryRows[i].selected.value;
    s.inactiveCategoryColors[i]          = m_categoryRows[i].inactiveNormal.value;
    s.inactiveSelectedCategoryColors[i]  = m_categoryRows[i].inactiveSelected.value;
  }

  s.addressForeground  = m_addressFgValue;
  s.addressBackground  = m_addressBgValue;
  s.archiveAddressForeground = m_archiveAddressFgValue;
  s.archiveAddressBackground = m_archiveAddressBgValue;
  s.compareDifferForeground   = m_compareDifferFgValue;
  s.compareDifferBackground   = m_compareDifferBgValue;
  s.compareOnlyHereForeground = m_compareOnlyHereFgValue;
  s.compareOnlyHereBackground = m_compareOnlyHereBgValue;
  s.cursorActiveColor  = m_cursorActiveValue;
  s.cursorInactiveColor= m_cursorInactiveValue;

  // 注意: colorRules と、ビュアー (テキスト/バイナリ/画像) のフォント・配色・
  // 透過は UI を持たないためここでは触らない。ビュアー欄は各プラグイン設定
  // ページが setScheme() で直接書き込む。
}

void AppearanceTab::applyThemeModeChange() {
  // 1. いまウィジェットに表示されている値を、現在編集中の側へ書き戻す
  saveToScheme(currentScheme());

  // 2. ラジオから新しい m_dialogMode を取得
  if      (m_modeLightRadio && m_modeLightRadio->isChecked()) m_dialogMode = ThemeMode::Light;
  else if (m_modeDarkRadio  && m_modeDarkRadio->isChecked())  m_dialogMode = ThemeMode::Dark;
  else                                                          m_dialogMode = ThemeMode::Auto;

  // 3. 編集対象側を再決定。Auto は OS を直接判定 (Settings の保存値ではない)。
  const ThemeMode prevSide = m_dialogEditingSide;
  m_dialogEditingSide = (m_dialogMode == ThemeMode::Auto)
                          ? Settings::instance().detectOsTheme()
                          : m_dialogMode;

  // 4. 新しい編集対象のスキームをウィジェットへロード
  loadFromScheme(currentScheme());
  // 4.5. プリセットコンボを編集対象側に合わせて再構築
  rebuildPresetCombo();

  // 5. Auto のときだけ "currently: Light/Dark" 表示を更新
  updateAutoEffectiveLabel();

  // 6. 編集対象側が実際に変わった場合のみ ViewersTab 等に通知
  if (prevSide != m_dialogEditingSide) {
    emit editingSideChanged(m_dialogEditingSide);
  }
}

void AppearanceTab::rebuildPresetCombo() {
  if (!m_themePresetCombo) return;

  // 編集対象側を判定: Light なら "light" ブロック、Dark なら "dark" を持つ
  // プリセットだけを並べる。両方持ちのプリセット (例: Solarized) はどちらでも残る。
  const bool wantLight = (m_dialogEditingSide == ThemeMode::Light);

  // 直前の選択 (id) を記憶しておき、再構築後に同じ id があれば復元する。
  const QString prevId = m_themePresetCombo->currentData().toString();

  QSignalBlocker block(m_themePresetCombo);
  m_themePresetCombo->clear();
  // 1 行目はプレースホルダ (= 何も適用しない)。
  m_themePresetCombo->addItem(tr("(Choose a preset...)"), QString());

  for (const auto& p : PresetIO::listBundledThemePresets()) {
    // 各プリセットを読み込んで含まれているブロックを判定
    QJsonObject obj;
    if (!PresetIO::loadJsonFromResource(p.resourcePath, obj).ok) continue;
    PresetIO::ThemeData data;
    if (!PresetIO::themeFromJson(obj, data).ok) continue;
    const bool hasLight = data.light.has_value();
    const bool hasDark  = data.dark.has_value();

    if ((wantLight && hasLight) || (!wantLight && hasDark)) {
      m_themePresetCombo->addItem(p.displayName, p.resourcePath);
    }
  }

  // 同じ id が残っていれば復元、無ければプレースホルダ (index 0) を選択
  int restoreIdx = 0;
  if (!prevId.isEmpty()) {
    for (int i = 0; i < m_themePresetCombo->count(); ++i) {
      if (m_themePresetCombo->itemData(i).toString() == prevId) {
        restoreIdx = i;
        break;
      }
    }
  }
  m_themePresetCombo->setCurrentIndex(restoreIdx);
}

void AppearanceTab::updateAutoEffectiveLabel() {
  if (!m_autoEffectiveLabel) return;
  if (m_dialogMode == ThemeMode::Auto) {
    m_autoEffectiveLabel->setText(
      m_dialogEditingSide == ThemeMode::Dark
        ? tr("(currently applying: Dark)")
        : tr("(currently applying: Light)"));
    m_autoEffectiveLabel->setVisible(true);
  } else {
    m_autoEffectiveLabel->setVisible(false);
  }
}

void AppearanceTab::onSelectFont() {
  bool ok;
  QFont font = QFontDialog::getFont(&ok, m_selectedFont, this, tr("Select Font"));
  if (ok) {
    m_selectedFont = font;
    m_fontButton->setText(QString("%1, %2pt").arg(font.family()).arg(font.pointSize()));
  }
}

void AppearanceTab::buildCategoryRow(QGridLayout* grid, int row,
                                     FileCategory cat, const QString& label,
                                     bool selected, bool inactive) {
  const int idx = static_cast<int>(cat);
  auto& row_ref = m_categoryRows[idx];
  auto pickSlot = [&row_ref](bool sel, bool inact) -> CategoryStateRow& {
    if (inact)  return sel ? row_ref.inactiveSelected : row_ref.inactiveNormal;
    return           sel ? row_ref.selected         : row_ref.normal;
  };
  CategoryStateRow& r = pickSlot(selected, inactive);

  grid->addWidget(new QLabel(label, this), row, 0);

  r.fgButton = new QPushButton(this);
  r.fgButton->setFixedWidth(80);
  grid->addWidget(r.fgButton, row, 1);
  connect(r.fgButton, &QPushButton::clicked, this, [this, idx, selected, inactive]() {
    auto& rr = m_categoryRows[idx];
    CategoryStateRow& slot = inactive
      ? (selected ? rr.inactiveSelected : rr.inactiveNormal)
      : (selected ? rr.selected         : rr.normal);
    QColor picked = QColorDialog::getColor(
      slot.value.foreground.isValid() ? slot.value.foreground : QColor(Qt::black),
      this, tr("Select Foreground Color"),
      QColorDialog::ShowAlphaChannel);
    if (picked.isValid()) {
      slot.value.foreground = picked;
      updateColorButton(slot.fgButton, picked);
    }
  });

  r.bgButton = new QPushButton(this);
  r.bgButton->setFixedWidth(80);
  grid->addWidget(r.bgButton, row, 2);
  connect(r.bgButton, &QPushButton::clicked, this, [this, idx, selected, inactive]() {
    auto& rr = m_categoryRows[idx];
    CategoryStateRow& slot = inactive
      ? (selected ? rr.inactiveSelected : rr.inactiveNormal)
      : (selected ? rr.selected         : rr.normal);
    QColor picked = QColorDialog::getColor(
      slot.value.background.isValid() ? slot.value.background : QColor(Qt::white),
      this, tr("Select Background Color"),
      QColorDialog::ShowAlphaChannel);
    if (picked.isValid()) {
      slot.value.background = picked;
      updateColorButton(slot.bgButton, picked);
    }
  });

  r.boldCheck = new QCheckBox(this);
  grid->addWidget(r.boldCheck, row, 3);
  connect(r.boldCheck, &QCheckBox::toggled, this,
          [this, idx, selected, inactive](bool checked) {
    auto& rr = m_categoryRows[idx];
    CategoryStateRow& slot = inactive
      ? (selected ? rr.inactiveSelected : rr.inactiveNormal)
      : (selected ? rr.selected         : rr.normal);
    slot.value.bold = checked;
  });
}

void AppearanceTab::updateColorButton(QPushButton* btn, const QColor& color) {
  if (!btn) return;
  if (!color.isValid()) {
    btn->setText(tr("(none)"));
    btn->setStyleSheet(QString());
    return;
  }
  btn->setText(color.name());
  // ボタンにプレビュー色を反映。文字色は輝度から自動選択。
  // setStyleSheet を当てるとネイティブのフォーカスリングが消えるので、
  // :focus でハイライトカラーの枠を出してキー操作でも視認できるようにする。
  const int luminance = (color.red() * 299 + color.green() * 587 + color.blue() * 114) / 1000;
  const QString textColor = (luminance > 160) ? "black" : "white";
  btn->setStyleSheet(QString(
      "QPushButton { background-color: %1; color: %2; border: 1px solid #888; "
                    "border-radius: 3px; padding: 2px 6px; }"
      "QPushButton:focus { border: 2px solid palette(highlight); padding: 1px 5px; }")
                       .arg(color.name(), textColor));
}

void AppearanceTab::onApplyThemePreset() {
  // コンボの currentIndexChanged で都度呼ばれる。プレースホルダ選択時は no-op。
  // 確認ダイアログは出さない: ダイアログ内編集なので、Cancel で破棄できる。
  if (!m_themePresetCombo || m_themePresetCombo->currentIndex() < 0) return;
  const QString resourcePath = m_themePresetCombo->currentData().toString();
  if (resourcePath.isEmpty()) return;  // "(Choose a preset...)" プレースホルダ

  QJsonObject obj;
  if (auto r = PresetIO::loadJsonFromResource(resourcePath, obj); !r.ok) {
    warn(this, tr("Apply Theme Preset"), r.error);
    return;
  }
  PresetIO::ThemeData data;
  if (auto r = PresetIO::themeFromJson(obj, data); !r.ok) {
    warn(this, tr("Apply Theme Preset"), r.error);
    return;
  }

  // ダイアログ内のシャドースキームを書き戻し → プリセット側を上書き →
  // 現在表示中の側を再ロード。両側持ちプリセットは両方を更新する。
  saveToScheme(currentScheme());
  if (data.light) m_dialogLight = *data.light;
  if (data.dark)  m_dialogDark  = *data.dark;
  loadFromScheme(currentScheme());
}

void AppearanceTab::onExportTheme() {
  // 現在ウィジェットに乗っている値を最新スナップショットに反映してから出力
  saveToScheme(currentScheme());

  PresetIO::ThemeData data;
  data.light = m_dialogLight;
  data.dark  = m_dialogDark;

  const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  const QString path = QFileDialog::getSaveFileName(
    this, tr("Export Theme"),
    defaultDir + QStringLiteral("/farman-theme.json"),
    tr("JSON Files (*.json)"));
  if (path.isEmpty()) return;

  if (auto r = PresetIO::exportThemeToFile(path, data); !r.ok) {
    warn(this, tr("Export Theme"), r.error);
  }
}

void AppearanceTab::onImportTheme() {
  const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  const QString path = QFileDialog::getOpenFileName(
    this, tr("Import Theme"), defaultDir,
    tr("JSON Files (*.json)"));
  if (path.isEmpty()) return;

  PresetIO::ThemeData data;
  if (auto r = PresetIO::importThemeFromFile(path, data); !r.ok) {
    warn(this, tr("Import Theme"), r.error);
    return;
  }

  saveToScheme(currentScheme());
  if (data.light) m_dialogLight = *data.light;
  if (data.dark)  m_dialogDark  = *data.dark;
  loadFromScheme(currentScheme());
}

void AppearanceTab::save() {
  auto& settings = Settings::instance();

  // 1. 現在ウィジェットに乗っている値を編集中側のシャドースキームへ最終反映
  saveToScheme(currentScheme());

  // 2. 両方のスキームを Settings へ流し込む (RMW)。
  //    colorRules フィールドは AppearanceTab/ViewersTab 共に編集 UI を持たない
  //    ので、fresh に残った既存値をそのまま温存するため、theme-dependent な
  //    フィールドはすべて m_dialogLight/Dark から overlay する。
  auto overlayThemeFields = [](const ColorScheme& src, ColorScheme& dst) {
    // メイン (ファイルリスト + ベース)
    dst.baseBackground    = src.baseBackground;
    dst.baseForeground    = src.baseForeground;
    dst.uiFont            = src.uiFont;
    dst.listFont          = src.listFont;
    dst.addressFont       = src.addressFont;
    dst.fileListRowHeight = src.fileListRowHeight;
    for (int i = 0; i < static_cast<int>(FileCategory::Count); ++i) {
      dst.categoryColors[i]                 = src.categoryColors[i];
      dst.selectedCategoryColors[i]         = src.selectedCategoryColors[i];
      dst.inactiveCategoryColors[i]         = src.inactiveCategoryColors[i];
      dst.inactiveSelectedCategoryColors[i] = src.inactiveSelectedCategoryColors[i];
    }
    dst.addressForeground   = src.addressForeground;
    dst.addressBackground   = src.addressBackground;
    dst.archiveAddressForeground = src.archiveAddressForeground;
    dst.archiveAddressBackground = src.archiveAddressBackground;
    dst.compareDifferForeground   = src.compareDifferForeground;
    dst.compareDifferBackground   = src.compareDifferBackground;
    dst.compareOnlyHereForeground = src.compareOnlyHereForeground;
    dst.compareOnlyHereBackground = src.compareOnlyHereBackground;
    dst.cursorActiveColor   = src.cursorActiveColor;
    dst.cursorInactiveColor = src.cursorInactiveColor;
    // ビュアー (テキスト / バイナリ / 画像) のフォント・配色・透過 Solid 色は
    // 各プラグイン設定ページが直接 setScheme() で書き込むため、ここでは
    // overlay しない (fresh に残る既存値を温存する)。
  };

  ColorScheme fresh;
  fresh = settings.scheme(ThemeMode::Light);
  overlayThemeFields(m_dialogLight, fresh);
  settings.setScheme(ThemeMode::Light, fresh);

  fresh = settings.scheme(ThemeMode::Dark);
  overlayThemeFields(m_dialogDark, fresh);
  settings.setScheme(ThemeMode::Dark, fresh);

  // 3. テーマモードを反映 (変更があれば m_ を新側へスワップ + save)
  settings.setThemeMode(m_dialogMode);

  // 4. テーマ非依存のグローバル設定
  settings.setUseInactivePaneColors(m_inactivePaneCheck->isChecked());
  settings.setCursorShape(
    static_cast<CursorShape>(m_cursorShapeCombo->currentData().toInt()));
  settings.setCursorThickness(m_cursorThicknessSpin->value());

  // 5. ビュアー設定 (拡張子 / MIME / encoding / zoom / フォント / 配色 / 透過)
  //    はすべて各ビュアーのプラグイン設定へ移設済み。外観タブは保存しない。
}

} // namespace Farman
