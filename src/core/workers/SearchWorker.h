#pragma once

#include "WorkerBase.h"
#include <QByteArray>
#include <QDateTime>
#include <QFileInfo>
#include <QStringList>

namespace Farman {

// 追加フィルタ。各 enabled が false のときは無視される。
struct SearchFilter {
  // サイズ範囲 (バイト)。minSize / maxSize のいずれか or 両方。
  bool   sizeEnabled    = false;
  qint64 minSize        = 0;
  qint64 maxSize        = 0;  // 0 で上限なし

  // 更新日時範囲。
  bool      modifiedEnabled = false;
  QDateTime modifiedFrom;        // 無効値で下限なし
  QDateTime modifiedTo;          // 無効値で上限なし

  // 内容テキスト検索 (byte-level マッチ)。
  bool       contentEnabled       = false;
  QByteArray contentBytes;
  bool       contentCaseSensitive = false;
  // 内容検索でスキャンする最大ファイルサイズ。これより大きいファイルは対象外。
  // (ハングを防ぐための保険、デフォルト 50 MiB)
  qint64     contentMaxScanBytes  = 50LL * 1024 * 1024;
};

// 検索対象の種別。
enum class SearchTarget {
  Files,        // ファイルのみ (既定)
  Directories,  // ディレクトリのみ
  Both,         // 両方
};

// 名前パターンに一致するファイル / ディレクトリを rootPath 以下から探すワーカー。
// 逐次 resultFound(path) を emit し、完了時に finished(true) を発行する。
// キャンセルされた場合は finished(false) で終了する。
//
// サブディレクトリは excludeDirPatterns（ディレクトリ名の glob パターン）
// にマッチするとそのサブツリーごとスキップする。シンボリックリンクは
// 無限再帰回避のため追跡しない。
//
// SearchFilter で size / modified / content の追加条件を AND で適用できる。
// ただし size / content はディレクトリには意味が無いので、ディレクトリは
// これらの条件を「満たさないもの」として扱う (= 有効なときは結果に出さない)。
// 「ディレクトリのみ」ではダイアログ側で両フィルタを無効化するので、この規則が
// 効くのは「両方」を選んだときだけ。
class SearchWorker : public WorkerBase {
  Q_OBJECT

public:
  SearchWorker(const QString&      rootPath,
               const QStringList&  namePatterns,
               const QStringList&  excludeDirPatterns,
               const QStringList&  excludeFilePatterns,
               bool                includeSubdirs,
               const SearchFilter& filter,
               SearchTarget        target = SearchTarget::Files,
               QObject*            parent = nullptr);

signals:
  void resultFound(const QString& path);

protected:
  void run() override;

private:
  void searchIn(const QString& dirPath);
  bool isExcludedDir(const QString& dirName) const;
  bool isExcludedFile(const QString& fileName) const;
  // fi (ファイル or ディレクトリ) が追加フィルタを満たすか。
  bool matchesFilter(const QFileInfo& fi) const;
  // 名前パターンに一致するか。ディレクトリ側は entryInfoList のフィルタを
  // 使わず自前で照合する (再帰と結果出力で必要な条件が違うため)。
  bool matchesNamePattern(const QString& name) const;
  bool fileContainsContent(const QString& filePath) const;

  QString      m_rootPath;
  QStringList  m_namePatterns;
  QStringList  m_excludeDirPatterns;
  QStringList  m_excludeFilePatterns;
  bool         m_includeSubdirs;
  SearchFilter m_filter;
  SearchTarget m_target;
};

} // namespace Farman
