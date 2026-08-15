#pragma once

#include "core/ArchiveFormatCatalog.h"
#include "settings/Settings.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QWidget>

class QLineEdit;
class QSpinBox;
class QTableWidget;
class QToolButton;

namespace Farman {

// 設定 → Archive ページ。アーカイブに関する設定を 1 箇所に集約する:
//   - 対応形式の一覧 (組み込み libarchive 形式 + アーカイブプラグイン形式を
//     同列に表示)。有効 / 無効の切り替えと、各行の「詳細...」ダイアログで
//     対応拡張子・作成時の既定 (圧縮レベル / 暗号化 / ファイル名の文字コード)
//     を編集する
//   - 共通設定 (一時展開先 / パスワード試行回数 / ネスト段数上限)
//
// アーカイブプラグインはここで「アーカイブ形式の 1 つ」として扱う。有効 / 無効の
// 切り替えに加え、ロード状況・エラー・パス等の診断情報も詳細ダイアログで見せる
// (かつて設定 → プラグインの Archive 一覧が持っていた役割)。
//
// 形式の素性は ArchiveFormatCatalog が持ち、このタブは「カタログ既定からの
// 上書き」だけを Settings へ書き戻す。プラグイン形式の有効 / 無効の書き込み先は
// 既存の Settings::setArchivePluginDisabled で、ビュアー側とは独立している。
class ArchiveTab : public QWidget {
  Q_OBJECT

public:
  explicit ArchiveTab(QWidget* parent = nullptr);
  ~ArchiveTab() override = default;

  void save();

  // 直前の save() で「次回起動から反映」の変更 (プラグイン形式の有効 / 無効)
  // があったか。SettingsDialog が Apply/OK 後の通知に使う。
  bool restartRequiredOnSave() const { return m_restartRequiredOnSave; }

protected:
  // 形式一覧のキー操作。PluginsTab の一覧と同じ作法に揃える:
  //   - Enter で選択行の詳細ダイアログ、Space で有効 / 無効のトグル
  //   - 行移動は ↑/↓ のみ (←/→ は無効化)
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void setupUi();
  void loadSettings();
  void loadFormatList();
  // 形式一覧の「詳細...」ダイアログ。一覧には最低限の列しか出さないので、
  // 対応拡張子と作成時の既定値の編集はこちらで行う。
  void showFormatDetails(int row);
  // 有効 / 無効の変更を編集状態と一覧表示の両方へ反映する。一覧のチェック
  // ボックスと詳細ダイアログの両方から呼ぶ。
  void setFormatEnabled(int row, bool enabled);

  // 編集中の 1 形式ぶんの状態。カタログ既定と一致する項目は save() で
  // 上書きとして書かない (既定への追従を保つため)。
  struct FormatState {
    ResolvedArchiveFormat resolved;   // 表示用 (info + ダイアログを開いた時点の実効値)
    bool        enabled = false;
    QStringList patterns;
    int         compressionLevel = -1;
    QString     encryption;
    QString     filenameEncoding;
  };

  // 有効 / 無効を切り替えられる行か (プラグイン ID を取得できた行だけ)。
  bool isToggleable(const FormatState& state) const;
  QString patternsDisplayText(const QStringList& patterns) const;
  // プラグイン形式のロード状態。組み込み形式では使わない。
  QString pluginStatusText(const ArchivePluginRecord& record) const;
  QString pluginStatusEmoji(const ArchivePluginRecord& record) const;
  QStringList parsePatterns(const QString& text) const;
  // 形式の由来 (組み込み / プラグイン) の表示文字列。
  QString originText(const ArchiveFormatInfo& info) const;

  // 形式一覧 (行番号 → 編集状態)。
  QTableWidget*     m_formatTable = nullptr;
  QList<FormatState> m_formats;

  // 共通設定
  QLineEdit*   m_tempDirectoryEdit    = nullptr;
  QToolButton* m_tempDirectoryBrowse  = nullptr;
  QToolButton* m_tempDirectoryDefault = nullptr;
  QSpinBox*    m_passwordRetrySpin    = nullptr;
  QSpinBox*    m_maxNestDepthSpin     = nullptr;

  // 一覧を作り直している間は itemChanged をユーザー操作として扱わない。
  bool m_populating = false;

  bool m_restartRequiredOnSave = false;
};

} // namespace Farman
