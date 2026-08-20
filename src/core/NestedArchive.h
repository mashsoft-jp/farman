#pragma once

#include <QString>

namespace Farman::NestedArchive {

// 入れ子アーカイブ (アーカイブの中のアーカイブ) を一時展開して実ファイルに
// する仕組み。libarchive はアーカイブの中のアーカイブを直接は読めないので、
// 内側を一旦ディスクへ書き出してから開き直す必要がある。
//
// ここが持つのは「論理パス → 展開済み実ファイル」の対応表と、その置き場所
// (セッション限りの一時ディレクトリ) だけ。展開そのものは、外側の
// ArchiveContext を持っている呼び出し側 (FileListModel) が行う。
//
// 論理パス (spec) は "/abs/a.zip!/d/inner.zip" の形式。

// spec に対して展開済みの実ファイルがあれば、そのパスを返す (無ければ空)。
// 大元のアーカイブ (チェーンの一番外側) が更新されていた場合は、展開済みの
// ファイルが古いので空を返す = 呼び出し側が展開し直す。
QString cachedLocalPath(const QString& spec);

// spec 用の展開先ファイルパスを決めて返す (親ディレクトリは作成済み)。
// 一時ディレクトリを用意できなければ空を返す。
QString reserveLocalPath(const QString& spec);

// 展開に成功したら登録する。以後 cachedLocalPath() が返すようになる。
void registerLocalPath(const QString& spec, const QString& localPath);

} // namespace Farman::NestedArchive
