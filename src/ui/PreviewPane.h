#pragma once

#include <QWidget>

class QLabel;
class QStackedWidget;

namespace Farman {

// プレビューモード時に Splitter の右側に常駐するウィジェット。
//   - Dual / Single 中は hide() されており、Preview に切替えた瞬間だけ
//     show() される。
//   - Phase 0 段階では中身は QLabel 一枚の "Preview mode (under construction)"
//     プレースホルダ。Phase 1 以降で TextView / ImageView / BinaryView +
//     Empty / Loading / Unsupported ページを内包する QStackedWidget へ
//     差し替える。
class PreviewPane : public QWidget {
  Q_OBJECT
public:
  explicit PreviewPane(QWidget* parent = nullptr);
  ~PreviewPane() override = default;

  // Phase 0 用の最小 API。Phase 1 で setTarget(path, displayPath, isDir, size)
  // 等に拡張する。
  void clear();

private:
  void setupUi();

  QStackedWidget* m_stack = nullptr;
  QLabel*         m_placeholderLabel = nullptr;
};

} // namespace Farman
