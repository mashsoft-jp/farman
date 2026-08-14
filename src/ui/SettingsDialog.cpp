#include "SettingsDialog.h"
#include "KeybindingTab.h"
#include "AppearanceTab.h"
#include "BehaviorTab.h"
#include "PluginsTab.h"
#include "ArchiveTab.h"
#include "GeneralTab.h"
#include "ExternalAppsTab.h"
#include "settings/Settings.h"
#include "keybinding/KeyBindingManager.h"
#include "keybinding/ViewerKeyBindingManager.h"
#include "viewer/ViewerDispatcher.h"
#include "utils/Dialogs.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <cstdlib>

namespace Farman {

namespace {

// farman を再起動する: 新プロセスを起動してから旧プロセスを即終了する。
// QApplication::quit() はモーダルダイアログの内側から呼ぶと外側の
// event loop が終了せず、旧プロセスが残ったまま新プロセスが立ち上がる
// ことがあるため、Settings の save() 後に Qt のクリーンアップを
// スキップして即終了する。
[[noreturn]] void restartFarman() {
#ifdef Q_OS_MACOS
  // applicationFilePath() は <bundle>.app/Contents/MacOS/<exe> を返す。
  // 裸の exe を直接起動すると LaunchServices が別アプリ扱いし、Dock に
  // も別エントリで現れるので .app の根を open(1) -n で起動する。
  QString bundlePath = QApplication::applicationFilePath();
  const int idx = bundlePath.indexOf(QStringLiteral("/Contents/MacOS/"));
  if (idx > 0) bundlePath.truncate(idx);
  QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                          QStringList{QStringLiteral("-n"), bundlePath});
#else
  QProcess::startDetached(QApplication::applicationFilePath(), QStringList());
#endif
  // プラグイン契約 (アンロード前に shutdown() を 1 回) をこの経路でも守る。
  // _Exit はデストラクタを走らせないため、アンロード済みコードに触れる
  // 後続処理は無い。
  ViewerDispatcher::instance().shutdownPlugins();
  std::_Exit(0);
}

} // namespace

SettingsDialog::SettingsDialog(const QString& leftCurrentPath,
                               const QString& rightCurrentPath,
                               const QSize&   currentWindowSize,
                               const QPoint&  currentWindowPosition,
                               QWidget* parent)
  : QDialog(parent)
  , m_sideMenu(nullptr)
  , m_stackedWidget(nullptr)
  , m_keybindingTab(nullptr)
  , m_appearanceTab(nullptr)
  , m_behaviorTab(nullptr)
  , m_pluginsTab(nullptr)
  , m_archiveTab(nullptr)
  , m_externalAppsTab(nullptr)
  , m_buttonBox(nullptr)
  , m_leftCurrentPath(leftCurrentPath)
  , m_rightCurrentPath(rightCurrentPath)
  , m_currentWindowSize(currentWindowSize)
  , m_currentWindowPosition(currentWindowPosition) {
  setupUi();
}

void SettingsDialog::setupUi() {
  setWindowTitle(tr("Settings"));
  // 横方向はスクロールしないので、ファイル種別カラーや拡張子 LineEdit が
  // 1 行に収まる幅を確保する。縦は QScrollArea でラップしているので超えても OK。
  resize(1000, 620);

  // メインは上下 2 段: 上 = サイドメニュー + ページ、下 = ボタン行
  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // ── サイドメニュー + 右ペイン (QStackedWidget) ──
  QHBoxLayout* contentLayout = new QHBoxLayout();
  contentLayout->setSpacing(8);

  m_sideMenu = new QListWidget(this);
  m_sideMenu->setFixedWidth(160);
  m_sideMenu->setFocusPolicy(Qt::StrongFocus);
  // 選択行のスタイル:
  //   - :active = サイドメニューがアクティブ (フォーカス有) → palette(highlight)
  //   - :!active = フォーカスが右側ペインへ移った → palette(mid) の薄い色
  // サイドメニュー自体 (QListWidget) の背景には触らない。padding は項目余白。
  m_sideMenu->setStyleSheet(QStringLiteral(
    "QListWidget::item { padding: 6px 8px; }"
    "QListWidget::item:selected:active { "
        "background-color: palette(highlight); "
        "color: palette(highlighted-text); }"
    "QListWidget::item:selected:!active { "
        "background-color: palette(mid); "
        "color: palette(window-text); }"
  ));
  contentLayout->addWidget(m_sideMenu);

  m_stackedWidget = new QStackedWidget(this);
  contentLayout->addWidget(m_stackedWidget, /*stretch*/ 1);

  // ── 各ページ生成 ──
  m_keybindingTab   = new KeybindingTab(this);
  m_appearanceTab   = new AppearanceTab(this);
  m_behaviorTab     = new BehaviorTab(this);
  m_pluginsTab      = new PluginsTab(this);
  m_archiveTab      = new ArchiveTab(this);
  m_generalTab      = new GeneralTab(m_leftCurrentPath, m_rightCurrentPath,
                                     m_currentWindowSize, m_currentWindowPosition,
                                     this);
  m_externalAppsTab = new ExternalAppsTab(this);

  // ページとメニュー項目を 1:1 で並べる。順序は旧 TabWidget と同じ。
  // 外部アプリはキーバインドと密結合 (T / E などのキーが指す先) なので
  // Keybindings の直前に並べる。
  // 各ページは QScrollArea でラップする。これにより縦に長いタブ (主に
  // Appearance のサブタブ群) でも縦スクロールで全項目にアクセスできる。
  auto addPage = [this](QWidget* page, const QString& label) {
    m_sideMenu->addItem(label);
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);            // page を scroll の幅に追従
    scroll->setFrameShape(QFrame::NoFrame);      // 余計な枠線を出さない
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // 横は不要
    scroll->setWidget(page);
    m_stackedWidget->addWidget(scroll);
  };
  // 順序は Page enum と 1:1 (setCurrentPage が enum 値を行番号として使う)。
  addPage(m_generalTab,      tr("1. General"));
  addPage(m_behaviorTab,     tr("2. Behavior"));
  addPage(m_appearanceTab,   tr("3. Appearance"));
  addPage(m_pluginsTab,      tr("4. Plugins"));
  addPage(m_archiveTab,      tr("5. Archive"));
  addPage(m_externalAppsTab, tr("6. External Apps"));
  addPage(m_keybindingTab,   tr("7. Keybindings"));

  m_sideMenu->setCurrentRow(0);
  connect(m_sideMenu, &QListWidget::currentRowChanged,
          m_stackedWidget, &QStackedWidget::setCurrentIndex);

  mainLayout->addLayout(contentLayout, /*stretch*/ 1);

  // macOS の System Settings → 「キーボード」→「キーボードナビゲーション」に
  // 依存させたくないので、各ページ内の操作対象ウィジェットに対して明示的に
  // StrongFocus を設定する。Tab キーで全項目を辿れるようにするのが目的。
  const QList<QWidget*> tabRoots = {
    m_generalTab, m_behaviorTab, m_appearanceTab,
    m_pluginsTab, m_archiveTab, m_externalAppsTab, m_keybindingTab
  };
  for (QWidget* root : tabRoots) {
    const auto widgets = root->findChildren<QWidget*>();
    for (QWidget* w : widgets) {
      if (qobject_cast<QCheckBox*>(w)   || qobject_cast<QComboBox*>(w) ||
          qobject_cast<QSpinBox*>(w)    || qobject_cast<QLineEdit*>(w) ||
          qobject_cast<QPushButton*>(w) || qobject_cast<QToolButton*>(w)) {
        w->setFocusPolicy(Qt::StrongFocus);
      }
      // macOS では QToolButton にネイティブのフォーカスリングが描かれず、
      // Tab で到達しても視認できない。独自スタイルを持たないものに限り、
      // ボタン風の通常時スタイル + フォーカス時のハイライト枠を一律に当てる
      // (ExternalAppsTab の browse ボタンと同じ手法)。
      if (auto* toolBtn = qobject_cast<QToolButton*>(w)) {
        if (toolBtn->styleSheet().isEmpty()) {
          toolBtn->setStyleSheet(QStringLiteral(
            "QToolButton { padding: 2px 6px; border: 1px solid palette(mid); "
                          "border-radius: 4px; "
                          "background-color: palette(button); }"
            "QToolButton:pressed { background-color: palette(midlight); }"
            "QToolButton:focus { border: 2px solid palette(highlight); "
                                "padding: 1px 5px; }"));
        }
      }
    }
  }

  // Alt+1 〜 Alt+9 でカテゴリ直接ジャンプ
  const QList<Qt::Key> numberKeys = {
    Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4, Qt::Key_5,
    Qt::Key_6, Qt::Key_7, Qt::Key_8, Qt::Key_9
  };
  const int pageCount = m_sideMenu->count();
  for (int i = 0; i < qMin(pageCount, numberKeys.size()); ++i) {
    QShortcut* shortcut = new QShortcut(QKeySequence(Qt::ALT | numberKeys[i]), this);
    connect(shortcut, &QShortcut::activated, [this, i]() {
      m_sideMenu->setCurrentRow(i);
    });
  }
  // Ctrl+Tab / Ctrl+Shift+Tab で前後カテゴリへ
  QShortcut* shortcutNext = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), this);
  connect(shortcutNext, &QShortcut::activated, [this]() {
    int n = m_sideMenu->count();
    if (n > 0) m_sideMenu->setCurrentRow((m_sideMenu->currentRow() + 1) % n);
  });
  QShortcut* shortcutPrev = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this);
  connect(shortcutPrev, &QShortcut::activated, [this]() {
    int n = m_sideMenu->count();
    if (n > 0) m_sideMenu->setCurrentRow((m_sideMenu->currentRow() - 1 + n) % n);
  });

  // OK/Cancel/Apply buttons + 全設定リセット (キーバインドを除く)
  m_buttonBox = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply
      | QDialogButtonBox::RestoreDefaults,
    this
  );
  // RestoreDefaults を「全設定リセット」用に流用。Keybindings タブ専用の
  // Reset to Defaults は KeybindingTab 内のボタンで継続。
  if (auto* resetBtn = m_buttonBox->button(QDialogButtonBox::RestoreDefaults)) {
    resetBtn->setText(tr("Reset All Settings..."));
    resetBtn->setToolTip(
      tr("Reset every setting (except keybindings) to its default value."));
    connect(resetBtn, &QPushButton::clicked, this, &SettingsDialog::onResetAllSettings);
  }
  auto* okBtn     = m_buttonBox->button(QDialogButtonBox::Ok);
  auto* cancelBtn = m_buttonBox->button(QDialogButtonBox::Cancel);
  auto* applyBtn  = m_buttonBox->button(QDialogButtonBox::Apply);
  applyAltShortcut(okBtn,     Qt::Key_O);
  applyAltShortcut(cancelBtn, Qt::Key_X);
  applyAltShortcut(applyBtn,  Qt::Key_A);
  // 誤操作防止のため Cancel → Apply → OK の順で Tab が回るようにする
  setTabOrder(cancelBtn, applyBtn);
  setTabOrder(applyBtn,  okBtn);

#if defined(Q_OS_MAC)
  // macOS の QMacStyle は、ボタンの高さがわずか (1px) でも標準と異なると、
  // 丸型のプッシュボタンではなく角型ベゼルで描画する。Apply だけ他より
  // 1px 高くなり「適用」ボタンだけデザインが浮いて見えるため、ボタンボックス
  // 内の全ボタンの高さを最小値に揃えて描画を統一する。
  {
    int uniformH = -1;
    for (QAbstractButton* b : m_buttonBox->buttons()) {
      const int h = b->sizeHint().height();
      if (uniformH < 0 || h < uniformH) uniformH = h;
    }
    if (uniformH > 0) {
      for (QAbstractButton* b : m_buttonBox->buttons()) {
        b->setFixedHeight(uniformH);
      }
    }
  }
#endif

  connect(m_buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::onOk);
  connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(applyBtn, &QPushButton::clicked, this, &SettingsDialog::onApply);

  mainLayout->addWidget(m_buttonBox);

  // Keyboard shortcuts for Keybindings tab
  m_clearShortcut = new QShortcut(QKeySequence("Ctrl+D"), this);
  connect(m_clearShortcut, &QShortcut::activated, this, &SettingsDialog::onClearBinding);

  m_resetShortcut = new QShortcut(QKeySequence("Ctrl+R"), this);
  connect(m_resetShortcut, &QShortcut::activated, this, &SettingsDialog::onResetToDefaults);
}

void SettingsDialog::setCurrentPage(Page page) {
  m_sideMenu->setCurrentRow(static_cast<int>(page));
}

void SettingsDialog::onOk() {
  onApply();
  accept();
}

void SettingsDialog::onApply() {
  // Save all tabs
  m_keybindingTab->save();
  m_appearanceTab->save();
  m_behaviorTab->save();
  m_pluginsTab->save();
  m_archiveTab->save();
  m_generalTab->save();
  m_externalAppsTab->save();

  // Save settings to file
  Settings::instance().save();

  // Save keybindings to settings
  KeyBindingManager::instance().saveToSettings();

  // 同梱ビュアーのショートカット (ビュアー別スコープ) も永続化する。ここで
  // ViewerKeyBindingManager::bindingsChanged が発火し、開いているビュアーへ
  // 割り当てが再 push される (一方向：本体→ビュアー)。
  ViewerKeyBindingManager::instance().saveToSettings();

  // Notify that settings have changed
  emit settingsChanged();

  // プラグインの有効/無効・ディレクトリは次回起動から反映されるため、
  // 再起動するか確認し、Yes なら即再起動する。
  // Y/N の単押し対応のため独自の confirm() ヘルパを使う。
  if (m_pluginsTab->restartRequiredOnSave()
      || m_archiveTab->restartRequiredOnSave()) {
    if (confirm(this,
                tr("Plugins"),
                tr("Plugin changes will take effect after restarting farman.\n"
                   "Restart farman now?"),
                /*defaultYes=*/true)) {
      restartFarman();
    }
  }

  // 言語が変わっていたら、現プロセスでは適切に切り替えられないので
  // 再起動を促す。
  if (m_generalTab->languageChangedOnSave()) {
    if (confirm(this,
                tr("Language Changed"),
                tr("Restart farman now to apply the new language?"),
                /*defaultYes=*/true)) {
      restartFarman();
    }
  }
}

void SettingsDialog::onClearBinding() {
  qDebug() << "SettingsDialog::onClearBinding()";
  // Only work if Keybindings tab is active
  if (m_stackedWidget->currentWidget() == m_keybindingTab) {
    m_keybindingTab->clearCurrentBinding();
  }
}

void SettingsDialog::onResetAllSettings() {
  if (!confirm(this, tr("Reset All Settings"),
               tr("Reset all settings to their default values?\n"
                  "Keybindings are not affected."),
               /*defaultYes=*/false)) {
    return;
  }
  // メモリ上のフィールドをすべてデフォルトに戻し、ファイルへも保存。
  // 各タブを丸ごと再構築するのは大変なので、ダイアログを閉じて呼び出し側に
  // 「設定が変わった」を通知する。ユーザーが Settings をもう一度開けば
  // フレッシュなデフォルト値が反映される。
  Settings::instance().resetToDefaults();
  Settings::instance().save();
  emit settingsChanged();
  accept();
}

void SettingsDialog::onResetToDefaults() {
  qDebug() << "SettingsDialog::onResetToDefaults()";
  // Only work if Keybindings tab is active
  if (m_stackedWidget->currentWidget() == m_keybindingTab) {
    m_keybindingTab->resetToDefaults();
  }
}

void SettingsDialog::keyPressEvent(QKeyEvent* event) {
  // Only process shortcuts when Keybindings tab is active
  if (m_stackedWidget->currentWidget() == m_keybindingTab) {
    // Ctrl+D (Cmd+D on macOS) for Clear Binding
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_D) {
      qDebug() << "Ctrl+D pressed via keyPressEvent";
      onClearBinding();
      event->accept();
      return;
    }
    // Ctrl+R (Cmd+R on macOS) for Reset to Defaults
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_R) {
      qDebug() << "Ctrl+R pressed via keyPressEvent";
      onResetToDefaults();
      event->accept();
      return;
    }
  }

  // Pass unhandled events to parent class
  QDialog::keyPressEvent(event);
}

} // namespace Farman
