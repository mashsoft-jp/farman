#pragma once

#include "settings/Settings.h"
#include "settings/ColorScheme.h"
#include <QWidget>
#include <QFont>
#include <QColor>

class QPushButton;
class QComboBox;
class QGroupBox;
class QCheckBox;
class QRadioButton;
class QGridLayout;
class QSpinBox;
class QLabel;
class QLineEdit;
class QTabWidget;

namespace Farman {

class AppearanceTab : public QWidget {
  Q_OBJECT

public:
  explicit AppearanceTab(QWidget* parent = nullptr);
  ~AppearanceTab() override = default;

  void save();

  // 「今ダイアログ内でどちら側 (Light/Dark) のスキームを編集しているか」を返す。
  // 他タブから編集対象側を取得するためのアクセサ (現状は未使用 — ViewersTab を
  // 統合した結果 connection が不要になったが API は残しておく)。
  ThemeMode editingSide() const { return m_dialogEditingSide; }

protected:
  // m_subTabs の QTabBar に対する FocusIn/FocusOut を捕捉し、フォーカスが
  // 外れたときだけ「選択中タブ」の色を淡くするための event filter。stylesheet
  // を動的に切替えることで、フォーカス時は native の青、非フォーカス時は
  // muted な見た目に変える。
  bool eventFilter(QObject* watched, QEvent* event) override;

signals:
  // Mode ラジオ変更で編集対象側 (Light/Dark) が切替わったときに発火。
  // ViewersTab がこれを受けて自分側の編集対象もスワップする。
  void editingSideChanged(ThemeMode newSide);

private slots:
  void onSelectFont();
  // テーマプリセット適用 / インポート / エクスポート (確認ダイアログ → 反映)。
  // 反映先は「現在編集中の側」(Auto なら effectiveTheme)。両側を持つプリセット
  // は両側を更新する。アクティブ側は m_ にも適用 → 即時プレビュー。
  void onApplyThemePreset();
  void onExportTheme();
  void onImportTheme();

private:
  void setupUi();
  void loadSettings();

  // ── サブタブ構築 (メイン / テキスト / バイナリ / 画像) ─────
  // 設定ダイアログでは Theme グループ (Mode + Preset) の下に QTabWidget を
  // 置き、メイン (ファイルリスト) と 3 種ビュアー (Text/Binary/Image) を
  // 切替える。各ビュアー sub-tab はかつての ViewersTab と同等の内容を持つ。
  QWidget* buildMainPage();
  QWidget* buildTextViewerPage();
  QWidget* buildBinaryViewerPage();
  QWidget* buildImageViewerPage();

  // ── Light / Dark スキーム編集 ─────────────────
  // ダイアログ内では Light/Dark 双方の ColorScheme をシャドーで保持する。
  // ウィジェットには「現在編集中の側」だけが表示され、Mode を切替える
  // と現状値を該当スキームへ書き戻してから反対側を読み直す。
  // OK 押下時に setScheme(Light, ...) / setScheme(Dark, ...) で両側を
  // Settings に流し込み、最後に setThemeMode で適用。
  ColorScheme& currentScheme();
  void         loadFromScheme(const ColorScheme& s);
  void         saveToScheme(ColorScheme& s) const;
  // テーマモードコンボの状態を m_dialogMode に反映 + 編集対象を再計算 + 再ロード
  void         applyThemeModeChange();
  // Auto モード時のみ「現在 OS 側がどちらを要求しているか」を表示するラベルを
  // 表示/非表示 + 文言更新する。Light / Dark を明示選択中は隠す。
  void         updateAutoEffectiveLabel();

  // カテゴリ×状態（通常／選択）1 組のウィジェットをまとめた構造
  struct CategoryStateRow {
    QPushButton*  fgButton  = nullptr;
    QPushButton*  bgButton  = nullptr;
    QCheckBox*    boldCheck = nullptr;
    CategoryColor value;  // 編集中の値
  };
  struct CategoryRow {
    CategoryStateRow normal;           // active / normal
    CategoryStateRow selected;         // active / selected
    CategoryStateRow inactiveNormal;   // inactive / normal
    CategoryStateRow inactiveSelected; // inactive / selected
  };
  // 1 行分のウィジェット（1 状態ぶん）を構築
  void buildCategoryRow(QGridLayout* grid, int row,
                        FileCategory cat, const QString& label,
                        bool selected, bool inactive);
  // 色ボタンにプレビュー色を反映
  void updateColorButton(QPushButton* btn, const QColor& color);

  // Base (UI default) settings — 個別設定で覆われない全 UI の地色 + 文字 + フォント
  QPushButton*   m_uiFontButton    = nullptr;
  QFont          m_uiFontValue;
  QPushButton*   m_baseFgButton    = nullptr;
  QPushButton*   m_baseBgButton    = nullptr;
  QColor         m_baseFgValue;
  QColor         m_baseBgValue;

  // Font settings
  QPushButton*   m_fontButton;          // File list font
  QFont          m_selectedFont;
  QPushButton*   m_addressFontButton = nullptr;
  QFont          m_addressFontValue;

  // ファイルサイズ・日時の表示形式は Behavior タブ側に移動した

  // ファイルリスト 1 行の縦幅 (0 = Auto)
  QSpinBox*      m_rowHeightSpin = nullptr;

  // Category colors (Normal / Hidden / Directory)
  CategoryRow m_categoryRows[static_cast<int>(FileCategory::Count)];
  // 非アクティブパネル設定: 外枠 QGroupBox はタイトル無しの単なる枠で、
  // 中の m_inactivePaneCheck (QCheckBox) が ON/OFF と「非アクティブパネル」
  // 見出しを兼ねる。QCheckBox を使うのは、macOS native のフォーカスリング
  // (青いハロー) が QGroupBox::indicator subcontrol には描画されず、実物の
  // QWidget (= QCheckBox) にしか付かないため。
  QGroupBox*  m_inactivePaneGroup = nullptr;
  QCheckBox*  m_inactivePaneCheck = nullptr;
  // チェック OFF 時にグレーアウトさせる対象 (Inactive 側 state grid 2 つ)
  QGroupBox*  m_inactiveNormalGrid   = nullptr;
  QGroupBox*  m_inactiveSelectedGrid = nullptr;

  // Address bar colors
  QPushButton* m_addressFgButton = nullptr;
  QPushButton* m_addressBgButton = nullptr;
  QColor       m_addressFgValue;
  QColor       m_addressBgValue;
  // アーカイブ内ブラウジング中のアドレスバー色
  QPushButton* m_archiveAddressFgButton = nullptr;
  QPushButton* m_archiveAddressBgButton = nullptr;
  QColor       m_archiveAddressFgValue;
  QColor       m_archiveAddressBgValue;
  // ディレクトリ比較カラー (Differ / OnlyHere)
  QPushButton* m_compareDifferFgButton    = nullptr;
  QPushButton* m_compareDifferBgButton    = nullptr;
  QPushButton* m_compareOnlyHereFgButton  = nullptr;
  QPushButton* m_compareOnlyHereBgButton  = nullptr;
  QColor       m_compareDifferFgValue;
  QColor       m_compareDifferBgValue;
  QColor       m_compareOnlyHereFgValue;
  QColor       m_compareOnlyHereBgValue;
  // Cursor colors / shape
  QPushButton* m_cursorActiveButton   = nullptr;
  QPushButton* m_cursorInactiveButton = nullptr;
  QColor       m_cursorActiveValue;
  QColor       m_cursorInactiveValue;
  QComboBox*   m_cursorShapeCombo     = nullptr;
  QSpinBox*    m_cursorThicknessSpin  = nullptr;

  // ── テーマ (Light / Dark) ─────────────────────
  // モード切替はラジオボタン 3 つ (Auto / Light / Dark)。
  QRadioButton* m_modeAutoRadio  = nullptr;
  QRadioButton* m_modeLightRadio = nullptr;
  QRadioButton* m_modeDarkRadio  = nullptr;
  // Mode = Auto のときだけ「現在 OS 側がどちらを要求しているか」を表示する。
  // Light / Dark を明示選択しているときは自明なので空文字 + 非表示にする。
  QLabel*       m_autoEffectiveLabel = nullptr;
  // テーマプリセット / インポート / エクスポート。プリセットコンボは
  // 編集対象側 (Light/Dark) に応じて kind フィルタ済みの内容で再構築される。
  // コンボ選択で即時プレビュー反映するため Apply ボタンは無し。
  QComboBox*    m_themePresetCombo   = nullptr;
  QPushButton*  m_themeExportButton  = nullptr;
  QPushButton*  m_themeImportButton  = nullptr;

  // 編集対象側に応じてプリセットコンボを再構築する。各 JSON が持つブロック
  // (light / dark / 両方) を見て、編集対象側に対応するブロックがあるテーマ
  // だけ並べる (= 両方持ちのもの (Solarized) はどちらでも残る)。
  void rebuildPresetCombo();
  // ダイアログ内でのモード設定 (OK で確定)
  ThemeMode     m_dialogMode        = ThemeMode::Auto;
  // 現在ウィジェットが表示しているのが Light か Dark か
  ThemeMode     m_dialogEditingSide = ThemeMode::Light;
  // 「未保存の変更を含む両側のスキームスナップショット」
  ColorScheme   m_dialogLight;
  ColorScheme   m_dialogDark;

  // ── サブタブ ────────────────────────────────
  // メイン (ファイルリスト) / テキスト / バイナリ / 画像 を切替える QTabWidget。
  // Theme グループ (Mode + Preset) の下に置く。
  QTabWidget*   m_subTabs = nullptr;

  // ── ビュアー設定 (旧 ViewersTab から統合) ─────
  // テーマ依存フィールド (font/colors) は loadFromScheme/saveToScheme で読み書き。
  // テーマ非依存フィールド (拡張子/MIME/エンコーディング/zoom 等) は loadSettings
  // と save で直接 Settings に対し読み書きする。
  // Viewer Display Mode (Inline / External) は Viewer Associations ページへ移動した。

  // Text viewer
  QLineEdit*   m_textExtensionsEdit       = nullptr;
  QLineEdit*   m_textMimePatternsEdit     = nullptr;
  QPushButton* m_textFontButton           = nullptr;
  QFont        m_textSelectedFont;
  // エンコーディング / 行番号 / 折り返しはテキストビュアープラグインの設定へ移設済み。
  QPushButton* m_textNormalFgButton       = nullptr;
  QPushButton* m_textNormalBgButton       = nullptr;
  QPushButton* m_textSelectedFgButton     = nullptr;
  QPushButton* m_textSelectedBgButton     = nullptr;
  QPushButton* m_textLineNumberFgButton   = nullptr;
  QPushButton* m_textLineNumberBgButton   = nullptr;
  QColor       m_textNormalFgValue;
  QColor       m_textNormalBgValue;
  QColor       m_textSelectedFgValue;
  QColor       m_textSelectedBgValue;
  QColor       m_textLineNumberFgValue;
  QColor       m_textLineNumberBgValue;

  // Image viewer
  QLineEdit*    m_imageExtensionsEdit           = nullptr;
  QLineEdit*    m_imageMimePatternsEdit         = nullptr;
  // ズーム / フィット / アニメは画像ビュアープラグインの設定へ移設済み。
  QRadioButton* m_imageTransparencyCheckerRadio = nullptr;
  QRadioButton* m_imageTransparencySolidRadio   = nullptr;
  QPushButton*  m_imageCheckerColor1Button      = nullptr;
  QPushButton*  m_imageCheckerColor2Button      = nullptr;
  QPushButton*  m_imageSolidColorButton         = nullptr;
  QColor        m_imageCheckerColor1Value;
  QColor        m_imageCheckerColor2Value;
  QColor        m_imageSolidColorValue;

  // Binary viewer
  // 単位 / エンディアン / エンコーディングはバイナリビュアープラグインの設定へ移設済み。
  QPushButton* m_binaryFontButton       = nullptr;
  QFont        m_binarySelectedFont;
  QPushButton* m_binaryNormalFgButton   = nullptr;
  QPushButton* m_binaryNormalBgButton   = nullptr;
  QPushButton* m_binarySelectedFgButton = nullptr;
  QPushButton* m_binarySelectedBgButton = nullptr;
  QPushButton* m_binaryAddressFgButton  = nullptr;
  QPushButton* m_binaryAddressBgButton  = nullptr;
  QColor       m_binaryNormalFgValue;
  QColor       m_binaryNormalBgValue;
  QColor       m_binarySelectedFgValue;
  QColor       m_binarySelectedBgValue;
  QColor       m_binaryAddressFgValue;
  QColor       m_binaryAddressBgValue;

  // (旧 m_pendingDateTimeFormat* は Behavior タブへ移動)
};

} // namespace Farman
