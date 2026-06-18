#pragma once

#include <QWidget>

namespace Farman {

// プラグインが farman の設定ダイアログ内に表示する「設定ページ」の抽象基底。
//
// farman 側 (Settings → Plugins → 詳細 → 設定...) がダイアログ枠
// (OK / Cancel / Apply / 既定に戻す) を用意し、ページをその中に載せる。
// プラグインはこの QWidget を継承して設定 UI を実装し、save() に永続化処理を
// 書くだけでよい (枠やボタンの面倒は farman が見る)。
//
// 流れ:
//   - 表示時: コンストラクタ等で現在値を UI に反映しておく。
//   - Apply / OK: farman が save() を呼ぶ → 値を settings.json 等へ永続化。
//     farman は save() 後に Settings を再読込し、開いているビュアーへ反映する。
//   - 既定に戻す: farman が restoreDefaults() を呼ぶ (UI を既定値へ。永続化は
//     しない。続けて Apply / OK されたら save() で書き込まれる)。
//
// Q_OBJECT は付けない (独自シグナル/スロットを持たない純粋な抽象 UI 基底)。
// 派生クラス側で必要に応じて Q_OBJECT を付ける。
class IPluginSettingsPage : public QWidget {
public:
  using QWidget::QWidget;
  ~IPluginSettingsPage() override = default;

  // Apply / OK で farman が呼ぶ。UI の現在値を永続化する。
  virtual void save() = 0;

  // 「既定に戻す」で farman が呼ぶ (任意)。UI を既定値へ戻す。
  virtual void restoreDefaults() {}
};

} // namespace Farman
