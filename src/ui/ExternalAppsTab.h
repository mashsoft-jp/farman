#pragma once

#include <QWidget>
#include "core/UserCommand.h"
#include "core/AppPresets.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLineEdit;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace Farman {

// Settings ダイアログの「外部アプリ (External Apps)」タブ。
// 組み込みの terminal / editor 2 件を専用 UI で編集する。ユーザー定義コマンドの
// 追加 / 編集 UI はフェーズ 2 で同タブに被せる予定。
class ExternalAppsTab : public QWidget {
  Q_OBJECT

public:
  explicit ExternalAppsTab(QWidget* parent = nullptr);
  ~ExternalAppsTab() override = default;

  void save();

protected:
  // Browse 用 QToolButton 群に対し、Enter / Return キーでもクリック扱いになる
  // ように KeyPress を捕捉する。デフォルトでは Space しか効かないため。
  bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
  void onBrowseTerminalProgram();
  void onBrowseEditorProgram();
  void onTestLaunchTerminal();
  void onTestLaunchEditor();
  // Preset コンボでプリセットが選ばれたとき、対応する Program / Arguments /
  // Working Dir フィールドを上書きする。"(Custom)" を選んだときは何もしない。
  void onTerminalPresetChanged(int index);
  void onEditorPresetChanged(int index);
  // ユーザー定義コマンド (= builtin terminal / editor 以外) の Export / Import。
  // SPEC.md L1564 の「ユーザー定義コマンドのインポート / エクスポート」を実装。
  // 組み込み 2 件は OS 依存なので対象外 (Import の Replace モードでも保持)。
  void onExportCommands();
  void onImportCommands();

  // ユーザー定義コマンドの「新規コマンド追加」フォーム (SPEC.md L681)。
  // 既存コマンドの編集 UI は各コマンドごとの QGroupBox に動的生成され、
  // ボタンの connect も lambda 内で完結する (per-row slot を持たない)。
  void onAddCommandClicked();          // 「追加」: 新規コマンドをモデルへ
  void onAddCommandTestClicked();      // 「テスト起動」: フォーム値で transient 実行
  void onAddCommandBrowseClicked();    // Program の Browse...

private:
  void setupUi();
  void loadSettings();

  // Preset コンボに「(Custom)」+ 検出済みプリセットを並べる。
  // userData には preset.id を入れる ("(Custom)" は空文字)。
  void populatePresetCombo(QComboBox* combo, const QList<AppPreset>& presets);

  // フィールドの textEdited を受けて、コンボを「(Custom)」に戻す。
  // ユーザーが手で書き換えた瞬間にプリセット表示が嘘になるのを防ぐ。
  void switchTerminalToCustom();
  void switchEditorToCustom();

  // Terminal
  // 作業ディレクトリはアクティブペインに固定 (= UserCommand.workingDirTemplate
  // を常に空) としたため、UI には Working Dir フィールドを持たない。
  QComboBox*   m_terminalPresetCombo    = nullptr;
  QLineEdit*   m_terminalProgramEdit    = nullptr;
  QToolButton* m_terminalProgramBrowse  = nullptr;
  QLineEdit*   m_terminalArgsEdit       = nullptr;
  QPushButton* m_terminalTestButton     = nullptr;

  // Text Editor
  QComboBox*   m_editorPresetCombo      = nullptr;
  QLineEdit*   m_editorProgramEdit      = nullptr;
  QToolButton* m_editorProgramBrowse    = nullptr;
  QLineEdit*   m_editorArgsEdit         = nullptr;
  QPushButton* m_editorTestButton       = nullptr;

  // タブ構築時に検出したプリセット一覧 (Combo の userData の id から逆引きする
  // ためにスナップショット保持)。
  QList<AppPreset> m_terminalPresets;
  QList<AppPreset> m_editorPresets;

  // Settings::userCommands() のうち builtin 以外のエントリ (= ユーザー定義
  // コマンド)。Add/Remove/編集の都度この QList を直接書き換え、save 時に
  // builtin 2 件と合わせて Settings に流し込む。
  QList<UserCommand> m_nonBuiltinUserCommands;

  // ── ユーザー定義コマンドの編集 UI ──
  // 「既存コマンドの編集フォーム × N」を縦に並べ、末尾に「新規コマンド追加」
  // フォーム (m_addCmdBox) を置く。外側のグループ枠は付けない (Add 用の枠と
  // 既存コマンドの枠の二重表示を避けるため)。新規追加時は m_customRowsLayout
  // の末尾 (= 追加フォームのすぐ上) に新しい QGroupBox を挿入する。
  QVBoxLayout*  m_customRowsLayout   = nullptr;

  // 「新規コマンド追加」フォーム本体 + 各フィールド + ボタン
  QGroupBox*    m_addCmdBox          = nullptr;
  QLineEdit*    m_addCmdName         = nullptr;
  QLineEdit*    m_addCmdProgram      = nullptr;
  QToolButton*  m_addCmdProgramBrowse= nullptr;
  QLineEdit*    m_addCmdArgs         = nullptr;
  QCheckBox*    m_addCmdShowInTools  = nullptr;
  QPushButton*  m_addCmdTestButton   = nullptr;
  QPushButton*  m_addCmdAddButton    = nullptr;

  // 既存コマンド 1 件ぶんのウィジェット束。lambda の per-row connect を
  // 配線する都合上、box (= QGroupBox*) と全フォーカス可能ウィジェットを
  // 持つ。並び順は m_nonBuiltinUserCommands と一致 (= UI 表示順)。
  // Update / Test 時はこのウィジェットから値を読み、id (= UserCommand.id)
  // で m_nonBuiltinUserCommands を引き当てて更新する。
  struct CustomCommandRow {
    QString      id;
    QWidget*     box              = nullptr;   // QGroupBox 実体
    QLineEdit*   nameEdit         = nullptr;
    QLineEdit*   programEdit      = nullptr;
    QToolButton* programBrowse    = nullptr;
    QLineEdit*   argsEdit         = nullptr;
    QCheckBox*   showInToolsCheck = nullptr;
    QToolButton* upBtn            = nullptr;   // ↑ (Tools メニューでの並び順)
    QToolButton* downBtn          = nullptr;   // ↓
    QPushButton* testBtn          = nullptr;
    QPushButton* updateBtn        = nullptr;
    QPushButton* deleteBtn        = nullptr;
  };
  QList<CustomCommandRow> m_customRows;

  // 指定 id のコマンドを 1 つ前 / 1 つ後に移動する。Tools メニューの並び順 +
  // ExternalApps タブの表示順がこの順序に従う。両端では何もしない。
  // m_nonBuiltinUserCommands / m_customRows / m_customRowsLayout の 3 つを
  // 同時に並び替えて整合させる。
  void moveCommandUp(const QString& id);
  void moveCommandDown(const QString& id);
  // 並び替え後の上 / 下端ボタンの enable 状態を全行ぶん更新する。
  void refreshReorderButtons();

  // 既存コマンド 1 件分の QGroupBox を構築し、m_customRows と
  // m_customRowsLayout に追加。command.id をキーに lambda を配線する。
  void buildExistingCommandRow(const UserCommand& cmd);
  // m_customRowsLayout の中身を一旦全クリアして m_nonBuiltinUserCommands
  // から構築し直す (loadSettings / onImportCommands 用)。
  void rebuildCustomCommandRows();
  // 各既存行のウィジェット内容を m_nonBuiltinUserCommands へ反映する。
  // save() 直前に呼び、未押下の「更新」ボタンの内容も拾うようにする。
  void flushCustomCommandRowsToModel();
  // Tab 順を「ターミナル → エディタ → 既存ユーザー定義コマンド 1, 2 ... → 新規
  // コマンド追加フォーム」に揃える。動的に追加 / 削除された行はデフォルト
  // ではチェーンの末尾に積まれてしまうので、行を増減するたびに本関数で
  // 全体を巻き直す。
  void rebuildCustomCommandTabOrder();
};

} // namespace Farman
