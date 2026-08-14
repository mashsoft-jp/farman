#pragma once

#include <QString>

namespace Farman::MarkdownSanitize {

// Qt の setMarkdown (GitHub dialect) は raw HTML をそのまま通す。そのため本文に
// 生の "<...>" があると HTML 開始タグと解釈され、対応する閉じタグが無いと
// **そこから後ろの本文が丸ごと表示されなくなる**。
//
// コードスパン / フェンスドコードブロックの外にある '<' だけを "&lt;" に置換して、
// タグ解釈を無効化した markdown を返す。'>' と '&' は setMarkdown が正しく扱うので
// 触らない (既に "&lt;DIR&gt;" のように明示エスケープしてある文書をそのまま活かす)。
// コード内も触らない (md4c がリテラルとして扱うので元から正しく出る)。
//
// setMarkdown に渡す前に必ずこれを通すこと。過去に 2 度同じ事故が起きている:
//   - Markdown ビュアーで型名 `shared_ptr<FileItem>` 以降が消えた (v0.9.7)
//   - 英語版のアップデート内容ダイアログが "<viewer name>" 以降で途切れた (v0.9.9)
QString neutralizeRawHtml(const QString& markdown);

} // namespace Farman::MarkdownSanitize
