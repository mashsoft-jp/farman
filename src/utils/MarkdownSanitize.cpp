#include "MarkdownSanitize.h"

#include <QStringList>

namespace Farman::MarkdownSanitize {

QString neutralizeRawHtml(const QString& markdown) {
  QString out;
  out.reserve(markdown.size() + 16);
  const QStringList lines = markdown.split(QLatin1Char('\n'));
  bool inFence = false;
  for (int li = 0; li < lines.size(); ++li) {
    const QString& line = lines.at(li);
    const QString trimmed = line.trimmed();
    const bool fenceLine = trimmed.startsWith(QLatin1String("```")) ||
                           trimmed.startsWith(QLatin1String("~~~"));
    if (fenceLine) {
      inFence = !inFence;
      out += line;  // フェンス行自体はそのまま
    } else if (inFence) {
      out += line;  // コードブロック内はそのまま
    } else {
      int i = 0;
      const int n = line.size();
      while (i < n) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('`')) {
          // バッククォートのラン長を測り、同じ長さの閉じまでをコードとして素通し
          int j = i;
          while (j < n && line.at(j) == QLatin1Char('`')) ++j;
          const int runLen = j - i;
          int k = j;
          int closeStart = -1;
          while (k < n) {
            if (line.at(k) == QLatin1Char('`')) {
              int m = k;
              while (m < n && line.at(m) == QLatin1Char('`')) ++m;
              if (m - k == runLen) { closeStart = k; break; }
              k = m;
            } else {
              ++k;
            }
          }
          if (closeStart >= 0) {
            out += line.mid(i, closeStart + runLen - i);
            i = closeStart + runLen;
            continue;
          }
          // 閉じが無ければ通常文字として扱う
        }
        if (c == QLatin1Char('<')) {
          out += QLatin1String("&lt;");
        } else {
          out += c;
        }
        ++i;
      }
    }
    if (li + 1 < lines.size()) {
      out += QLatin1Char('\n');
    }
  }
  return out;
}

} // namespace Farman::MarkdownSanitize
