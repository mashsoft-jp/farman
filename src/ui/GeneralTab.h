#pragma once

#include <QWidget>

class QComboBox;
class QCheckBox;
class QLabel;
class QSpinBox;
class QLineEdit;
class QToolButton;

namespace Farman {

// 「全般」タブ。アプリ全体／起動時の設定に加えてログ出力をまとめる。
//   - 各ペインの初期ディレクトリ
//   - ウィンドウサイズ / 位置
//   - UI 言語、終了時の確認
//   - ログ表示 / ログファイル出力 (パス・保持日数)
// 共通点: ファイル一覧の操作中に逐次 ON/OFF するような項目ではなく、
//         アプリ全体の挙動を決める「設定」寄りの項目を集めている。
class GeneralTab : public QWidget {
  Q_OBJECT

public:
  // currentWindowSize / currentWindowPosition: ダイアログ表示時点のメイン
  // ウィンドウの状態。「カスタムサイズ・位置」コンボのモードに関わらず、
  // SpinBox の初期値として常にこの値を出すために使う。
  GeneralTab(const QString& leftCurrentPath,
             const QString& rightCurrentPath,
             const QSize&   currentWindowSize,
             const QPoint&  currentWindowPosition,
             QWidget* parent = nullptr);
  ~GeneralTab() override = default;

  void save();

  // 直前の save() で言語設定が変更されたか。SettingsDialog 側で
  // 「再起動するか確認」ダイアログを出すために使う。
  bool languageChangedOnSave() const { return m_languageChangedOnSave; }

private slots:
  void onWindowSizeModeChanged(int index);
  void onWindowPositionModeChanged(int index);
  void onLeftInitialPathModeChanged(int index);
  void onRightInitialPathModeChanged(int index);
  void onLeftBrowseInitialPath();
  void onRightBrowseInitialPath();

  // 設定全体 (= settings.json) をユーザー指定の JSON ファイルへ書き出す /
  // から読み込む。ブックマーク・カスタムコマンド・カラースキーム等まとめて。
  void onExportAllSettings();
  void onImportAllSettings();

private:
  void setupUi();
  void loadSettings();

  // ダイアログ起動時の各ペインのカレントディレクトリ。
  // Custom Path 未設定時のデフォルトや、Default/LastSession 選択時の表示値として使う。
  QString m_leftCurrentPath;
  QString m_rightCurrentPath;

  // ダイアログ起動時のメインウィンドウのサイズ / 位置。
  // SpinBox のプリセットとして常にこれを使う (Custom モード以外でも
  // 「現在の状態」を見せるため)。
  QSize   m_currentWindowSize;
  QPoint  m_currentWindowPosition;

  // Initial directory per pane
  QComboBox*   m_leftInitialPathModeCombo  = nullptr;
  QLineEdit*   m_leftCustomPathEdit        = nullptr;
  QToolButton* m_leftBrowseButton          = nullptr;
  QComboBox*   m_rightInitialPathModeCombo = nullptr;
  QLineEdit*   m_rightCustomPathEdit       = nullptr;
  QToolButton* m_rightBrowseButton         = nullptr;
  QCheckBox*   m_confirmOnExitCheck        = nullptr;
  QCheckBox*   m_singleInstanceCheck       = nullptr;
  // メインツールバーの表示。View メニューからもトグル可能。
  QCheckBox*   m_showToolbarCheck          = nullptr;

  // Application language
  QComboBox*   m_languageCombo             = nullptr;

  // Window settings
  QComboBox*  m_windowSizeModeCombo        = nullptr;
  QSpinBox*   m_windowWidthSpin            = nullptr;
  QSpinBox*   m_windowHeightSpin           = nullptr;
  QComboBox*  m_windowPositionModeCombo    = nullptr;
  QSpinBox*   m_windowXSpin                = nullptr;
  QSpinBox*   m_windowYSpin                = nullptr;

  // Log settings
  QCheckBox*   m_logVisibleCheck           = nullptr;
  QSpinBox*    m_logPaneHeightSpin         = nullptr;
  QCheckBox*   m_logToFileCheck            = nullptr;
  QLineEdit*   m_logDirectoryEdit          = nullptr;
  QToolButton* m_logDirectoryBrowse        = nullptr;
  // 保持日数。0 を「永久」として扱うため、専用チェックボックス + SpinBox の組み合わせ
  QCheckBox*   m_logRetentionForeverCheck  = nullptr;
  QSpinBox*    m_logRetentionDaysSpin      = nullptr;

  // Viewer plugins
  QLineEdit*   m_pluginsDirectoryEdit       = nullptr;
  QToolButton* m_pluginsDirectoryBrowse     = nullptr;
  QToolButton* m_pluginsDirectoryDefault    = nullptr;

  // 直前の save() で言語が変更されたか
  bool         m_languageChangedOnSave     = false;

  // ── 自動アップデート (SPEC.md "自動アップデート" 節) ──
  // checkOnStartup: 起動時に最大 1 日 1 回 GitHub をチェック
  // silent:         確認ダイアログを出さずバックグラウンド更新
  // lastCheckedLabel: 前回チェック日時を読みやすい形式で表示
  // checkNowButton:   「今すぐチェック」ボタン
  QCheckBox*  m_autoUpdateCheckOnStartupCheck = nullptr;
  QCheckBox*  m_autoUpdateSilentCheck         = nullptr;
  QLabel*     m_autoUpdateLastCheckedLabel    = nullptr;
  QToolButton* m_autoUpdateCheckNowButton     = nullptr;
};

} // namespace Farman
