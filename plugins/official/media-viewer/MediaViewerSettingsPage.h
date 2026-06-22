#pragma once

#include "viewer/IPluginSettingsPage.h"

class QCheckBox;
class QLineEdit;
class QSpinBox;

namespace Farman {

// メディア (動画 / 音声) ビュアーの設定ページ (Settings → Plugins → 詳細)。
// 既定音量 / ループ再生 / 自動再生を扱う。
class MediaViewerSettingsPage : public IPluginSettingsPage {
  Q_OBJECT

public:
  explicit MediaViewerSettingsPage(QWidget* parent = nullptr);

  void save() override;
  void restoreDefaults() override;

private:
  void applyValuesToUi(const QStringList& extensions, int volume, bool loop,
                       bool autoplay);

  QLineEdit* m_extensionsEdit = nullptr;
  QSpinBox*  m_volumeSpin   = nullptr;
  QCheckBox* m_loopCheck     = nullptr;
  QCheckBox* m_autoplayCheck = nullptr;
};

} // namespace Farman
