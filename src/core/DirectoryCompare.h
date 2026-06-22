#pragma once

#include <QHash>
#include <QString>

namespace Farman {

// 1 エントリの比較結果。
// ペインごとに「name → DiffStatus」マップで保持する。
//   - Same:     反対側に同名エントリがあって内容も一致
//   - Differ:   反対側に同名エントリはあるが内容が違う (粒度依存)
//   - OnlyHere: このペインにしか無い
// "反対側にしかない" は、このペインのファイルリストにそもそも表示されないので
// このペインの map には現れない (反対ペインの map で OnlyHere として扱われる)。
enum class DiffStatus {
  Same,
  Differ,
  OnlyHere,
};

// 比較粒度。
//   - NameOnly:  ファイル名一致のみで判定 (size/mtime は見ない)
//   - SizeMtime: 名前 + size + mtime
enum class CompareGranularity {
  NameOnly,
  SizeMtime,
};

// 比較オプション。
struct CompareOptions {
  CompareGranularity granularity = CompareGranularity::SizeMtime;
  bool               recursive   = false;
};

// 各ペイン用の「name → DiffStatus」マップ。
using CompareOverlay = QHash<QString, DiffStatus>;

// 比較ワーカーの出力。左右両ペイン分の overlay を持つ。
struct CompareResult {
  CompareOverlay left;
  CompareOverlay right;
};

} // namespace Farman
