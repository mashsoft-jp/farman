#pragma once

#include "core/PluginInstaller.h"

#include <QStringList>
#include <QWidget>

class QAbstractItemView;
class QGroupBox;
class QPushButton;
class QVBoxLayout;

namespace Farman {

// 外部プラグインの導入・削除の UI。Settings → Viewer / Archive の一覧の下に置く。
// 仕様は SPEC.md「プラグインのインストール」。
//
//   - 「ファイルからインストール...」ボタンと、一覧へのドラッグ＆ドロップの受け口
//   - 再起動後に反映される変更 (導入待ち / 削除待ち) の一覧と取り消し
//
// 検証と退避そのものは PluginInstaller が行う。ここは確認ダイアログと結果表示を担う。
// 導入先の種別はプラグインの IID から自動で決まるので、どちらのタブのパネルに
// ドロップしても結果は同じ (両タブとも全種別の退避状況を表示する)。
class PluginInstallPanel : public QWidget {
  Q_OBJECT

public:
  explicit PluginInstallPanel(QWidget* parent = nullptr);

  // view (とその viewport) へのファイルドロップを導入操作として受け付ける。
  void watchDropTarget(QAbstractItemView* view);

  // 確認 → 検証 → 退避 → 結果表示までを行う。
  void installFiles(const QStringList& filePaths);

  // installedFilePath がプラグインディレクトリ配下 (アンインストール可能) か。
  static bool isManagedPluginFile(const QString& installedFilePath);
  bool isPendingRemoval(const QString& installedFilePath) const;
  bool isPendingUpdate(const QString& installedFilePath) const;

  // 確認のうえ削除を退避する。退避したら true。
  bool requestUninstall(const QString& installedFilePath, const QString& displayName);
  bool cancelUninstall(const QString& installedFilePath);

  // 退避状況を読み直して表示を更新する。
  void refresh();

signals:
  // 退避状況が変わった (一覧の状態表示を更新してもらうための通知)。
  void pendingChanged();
  // 導入フローの中でユーザーが「外部プラグインの読込みを許可」を ON にした。
  // 設定は保存済み。General タブのチェックボックスを同期させるための通知。
  void allowExternalPluginsEnabled();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void showEvent(QShowEvent* event) override;

private:
  void chooseFiles();
  void rebuildPendingList();
  static QString relativePathOf(const QString& installedFilePath);

  QPushButton* m_installButton = nullptr;
  QGroupBox*   m_pendingGroup  = nullptr;
  QVBoxLayout* m_pendingLayout = nullptr;
  PluginInstaller::PendingState m_pending;
};

} // namespace Farman
