#pragma once

#include <QDialog>

class QListWidget;
class QStackedWidget;
class QDialogButtonBox;
class QShortcut;

namespace Farman {

class KeybindingTab;
class AppearanceTab;
class BehaviorTab;
class ViewersTab;
class GeneralTab;
class ExternalAppsTab;

class SettingsDialog : public QDialog {
  Q_OBJECT

public:
  SettingsDialog(const QString& leftCurrentPath,
                 const QString& rightCurrentPath,
                 const QSize&   currentWindowSize,
                 const QPoint&  currentWindowPosition,
                 QWidget* parent = nullptr);
  ~SettingsDialog() override = default;

protected:
  void keyPressEvent(QKeyEvent* event) override;

signals:
  void settingsChanged();

private slots:
  void onOk();
  void onApply();
  void onClearBinding();
  void onResetToDefaults();
  // 「Reset All Settings」ボタン: キーバインドを除く全 Settings を
  // デフォルトに戻す (確認ダイアログ付き)。
  void onResetAllSettings();

private:
  void setupUi();

  QString m_leftCurrentPath;
  QString m_rightCurrentPath;
  QSize   m_currentWindowSize;
  QPoint  m_currentWindowPosition;

  // タブ → サイドメニュー化: 左に QListWidget でカテゴリ一覧、右に
  // QStackedWidget で対応ページを切替表示する。
  QListWidget*      m_sideMenu;
  QStackedWidget*   m_stackedWidget;
  KeybindingTab*    m_keybindingTab;
  AppearanceTab*    m_appearanceTab;
  BehaviorTab*      m_behaviorTab;
  ViewersTab*       m_viewersTab;
  GeneralTab*       m_generalTab;
  ExternalAppsTab*  m_externalAppsTab;
  QDialogButtonBox* m_buttonBox;
  QShortcut*      m_clearShortcut;
  QShortcut*      m_resetShortcut;
};

} // namespace Farman
