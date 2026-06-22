#!/usr/bin/env bash
# リリース前チェック: What's New (resources/whatsnew/*.md) がリリース対象
# バージョンの内容に更新されているかを検証する。
#
# 各ファイルの見出し (1 行目) に CMakeLists.txt の VERSION が含まれることを
# 確認する。バージョンはリリースごとに変わるため、このチェックを通すには
# 必ずファイルを編集することになり、「What's New を更新し忘れたまま
# リリースする」事故を防げる (内容そのものの正しさは人間が確認する)。
#
# 使い方:
#   tools/check_whatsnew.sh [vX.Y.Z(-suffix)]
#
# 引数にタグを渡すと「タグと CMakeLists.txt の VERSION の一致」も検証する
# (release.yml が tag push 時に渡す)。引数なしならローカル確認用に
# CMakeLists.txt の VERSION だけでチェックする。
set -euo pipefail
cd "$(dirname "$0")/.."

cmake_version=$(sed -nE 's/^project\(farman VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt)
if [ -z "${cmake_version}" ]; then
  echo "ERROR: CMakeLists.txt から VERSION を取得できませんでした" >&2
  exit 1
fi

if [ $# -ge 1 ]; then
  tag_no_v="${1#v}"
  tag_base="${tag_no_v%%-*}"
  if [ "${tag_base}" != "${cmake_version}" ]; then
    echo "ERROR: タグのバージョン (${tag_base}) と CMakeLists.txt の VERSION (${cmake_version}) が一致しません" >&2
    exit 1
  fi
fi

status=0
for f in resources/whatsnew/whatsnew_ja.md resources/whatsnew/whatsnew_en.md; do
  if [ ! -f "${f}" ]; then
    echo "ERROR: ${f} がありません" >&2
    status=1
    continue
  fi
  heading=$(head -n 1 "${f}")
  if [[ "${heading}" != *"${cmake_version}"* ]]; then
    echo "ERROR: ${f} の見出し「${heading}」にバージョン ${cmake_version} が含まれていません" >&2
    echo "       このリリースの内容に合わせて更新してから再実行してください" >&2
    status=1
  fi
done

if [ ${status} -eq 0 ]; then
  echo "OK: What's New は ${cmake_version} 向けに更新済みです"
fi
exit ${status}
