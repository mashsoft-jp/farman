#pragma once

#include <QWidget>

class QComboBox;

namespace Farman {

// 設定 → Viewers ページ。ビュアーの表示モード (Inline / External) を設定する。
// 拡張子の紐付け (Viewer Associations) はプラグイン関連の設定として
// PluginsTab へ移動した。
class ViewersTab : public QWidget {
  Q_OBJECT

public:
  explicit ViewersTab(QWidget* parent = nullptr);
  ~ViewersTab() override = default;

  void save();

private:
  void setupUi();
  void loadSettings();

  QComboBox* m_viewerModeCombo = nullptr;
};

} // namespace Farman
