#!/bin/bash
# farman デモ用データを /tmp/farman-demo に作り直す (left / right のみ触る)
set -euo pipefail
D=/tmp/farman-demo
rm -rf "$D/left" "$D/right"
mkdir -p "$D/left"/{archives,data,documents,images,projects/farman-plugin/src} "$D/right"/{backup,notes}

# documents
printf '# 議事録 2026-09\n\n- リリース計画の確認\n- 次回: 10/01\n' > "$D/left/documents/meeting-notes.md"
printf '# README\n\nfarman demo files.\n' > "$D/left/documents/README.md"
printf 'TODO\n- [ ] リリースノートを書く\n- [x] スクリーンショット更新\n' > "$D/left/documents/todo.txt"
printf 'memo: ランチは 12:30\n' > "$D/left/documents/memo.txt"
head -c 48211 /dev/urandom > "$D/left/documents/proposal.pdf"
head -c 23872 /dev/urandom > "$D/left/documents/budget-2026.xlsx"
head -c 31540 /dev/urandom > "$D/left/documents/report-2026Q3.docx"

# data
printf 'date,item,amount\n2026-09-01,license,12000\n2026-09-08,hosting,3300\n' > "$D/left/data/sales.csv"

# images: 本物の小さな JPEG を作る
i=0
for c in '#4f86c6' '#e07a5f' '#81b29a' '#f2cc8f' '#9d8189' '#3d5a80'; do
  i=$((i+1))
  magick -size 640x480 "gradient:${c}-#ffffff" -quality 82 "$D/left/images/IMG_40$((20+i)).jpg"
done

# archives
( cd "$D/left/documents" && zip -q "$D/left/archives/documents-backup.zip" README.md memo.txt todo.txt )
( cd "$D/left" && tar czf "$D/left/archives/photos-2026.tar.gz" images )

# 検索デモ用に md / txt を複数のディレクトリへ散らす
printf '# farman-plugin\n\nサンプルプラグイン。\n' > "$D/left/projects/farman-plugin/README.md"
printf 'sales.csv: 月次の売上データ\n' > "$D/left/data/readme.txt"

# projects
printf 'cmake_minimum_required(VERSION 3.21)\nproject(farman-plugin)\n' > "$D/left/projects/farman-plugin/CMakeLists.txt"
printf '#include <QtPlugin>\n' > "$D/left/projects/farman-plugin/src/plugin.cpp"

# right
printf '# アイデア\n\n- プラグインの自動インストール\n' > "$D/right/notes/ideas.md"
printf '# 2026-09 の振り返り\n' > "$D/right/notes/retrospective.md"
( cd "$D/left/data" && zip -q "$D/right/backup/old-backup.zip" sales.csv )

# 更新日時を揃える (ディレクトリも含め、深い方から)
find "$D/left" "$D/right" -depth -exec touch -t 202609151000.00 {} +
touch -t 202609151000.00 "$D"
echo "demo data ready"; find "$D/left" "$D/right" -type f | sort
