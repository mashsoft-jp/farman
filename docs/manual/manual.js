/* ===== farman manual — client-side keyword filter =====
 *
 * 検索ボックスに入力されたキーワードで、マニュアル内の項目を絞り込む。
 * - 各「項目」(.f-item: 表の行・リスト項目・注記) を対象に部分一致でフィルタ
 * - 空白区切りは AND 条件
 * - 見出しテキストに一致した場合はそのセクション/小見出しを丸ごと残す
 * - 一致項目のない見出し・セクション・目次は隠す
 * - 一致部分は <mark> でハイライト
 * 依存ゼロ・ビルド不要。
 */
(function () {
  "use strict";

  var input = document.getElementById("manual-q");
  var clearBtn = document.getElementById("manual-q-clear");
  var noResults = document.getElementById("manual-noresults");
  var toc = document.getElementById("manual-toc");
  if (!input) return;

  var items = Array.prototype.slice.call(document.querySelectorAll(".f-item"));
  var sections = Array.prototype.slice.call(document.querySelectorAll(".doc-section"));

  // 元の HTML を保存しておき、ハイライトの付け外しで使う。
  items.forEach(function (el) { el.dataset.orig = el.innerHTML; });

  function norm(s) { return (s || "").toLowerCase(); }

  function tokens(q) {
    return norm(q).split(/\s+/).filter(Boolean);
  }

  // テキストノードだけを対象に、各トークンを <mark> で包む (タグは壊さない)。
  function highlight(el, toks) {
    var walker = document.createTreeWalker(el, NodeFilter.SHOW_TEXT, null);
    var textNodes = [], n;
    while ((n = walker.nextNode())) textNodes.push(n);
    textNodes.forEach(function (node) {
      var text = node.nodeValue;
      var low = text.toLowerCase();
      // 一致位置を集める
      var ranges = [];
      toks.forEach(function (t) {
        var from = 0, idx;
        while ((idx = low.indexOf(t, from)) !== -1) {
          ranges.push([idx, idx + t.length]);
          from = idx + t.length;
        }
      });
      if (!ranges.length) return;
      // マージ
      ranges.sort(function (a, b) { return a[0] - b[0]; });
      var merged = [ranges[0].slice()];
      for (var i = 1; i < ranges.length; i++) {
        var last = merged[merged.length - 1];
        if (ranges[i][0] <= last[1]) last[1] = Math.max(last[1], ranges[i][1]);
        else merged.push(ranges[i].slice());
      }
      var frag = document.createDocumentFragment();
      var pos = 0;
      merged.forEach(function (r) {
        if (r[0] > pos) frag.appendChild(document.createTextNode(text.slice(pos, r[0])));
        var mark = document.createElement("mark");
        mark.textContent = text.slice(r[0], r[1]);
        frag.appendChild(mark);
        pos = r[1];
      });
      if (pos < text.length) frag.appendChild(document.createTextNode(text.slice(pos)));
      node.parentNode.replaceChild(frag, node);
    });
  }

  function matches(text, toks) {
    var low = norm(text);
    return toks.every(function (t) { return low.indexOf(t) !== -1; });
  }

  function apply(q) {
    var toks = tokens(q);
    var active = toks.length > 0;
    clearBtn.hidden = !active;

    if (!active) {
      // リセット
      items.forEach(function (el) {
        el.hidden = false;
        if (el.innerHTML !== el.dataset.orig) el.innerHTML = el.dataset.orig;
      });
      document.querySelectorAll(".f-subhead").forEach(function (h) { h.hidden = false; });
      document.querySelectorAll(".kb-table").forEach(function (t) { t.hidden = false; });
      sections.forEach(function (s) { s.hidden = false; });
      if (toc) toc.hidden = false;
      noResults.hidden = true;
      return;
    }

    var anyVisible = false;

    sections.forEach(function (section) {
      var sectionHit = false;
      var heading = section.querySelector("h2");
      var headingHit = heading && matches(heading.textContent, toks);

      // 小見出し (h3.f-subhead) ごとに、その見出し〜次の小見出し直前までの f-item を見る
      var localItems = section.querySelectorAll(".f-item");
      localItems.forEach(function (el) {
        var hit = headingHit || matches(el.dataset.orig.replace(/<[^>]+>/g, " "), toks);
        // restore then (maybe) highlight
        if (el.innerHTML !== el.dataset.orig) el.innerHTML = el.dataset.orig;
        el.hidden = !hit;
        if (hit) { sectionHit = true; if (!headingHit) highlight(el, toks); }
      });

      // 小見出しは、その配下に見えている f-item があるときだけ表示
      var subheads = section.querySelectorAll(".f-subhead");
      subheads.forEach(function (sub) {
        if (headingHit) { sub.hidden = false; return; }
        var visible = false, node = sub.nextElementSibling;
        while (node && !node.classList.contains("f-subhead")) {
          if (node.querySelectorAll) {
            var its = node.classList.contains("f-item") ? [node]
                      : Array.prototype.slice.call(node.querySelectorAll(".f-item"));
            if (its.some(function (x) { return !x.hidden; })) { visible = true; break; }
          }
          node = node.nextElementSibling;
        }
        sub.hidden = !visible;
      });

      // 行が 1 つも残らない表は、ヘッダーごと隠す
      section.querySelectorAll(".kb-table").forEach(function (tbl) {
        if (headingHit) { tbl.hidden = false; return; }
        var rows = tbl.querySelectorAll("tr.f-item");
        var anyRow = Array.prototype.some.call(rows, function (r) { return !r.hidden; });
        tbl.hidden = !anyRow;
      });

      section.hidden = !sectionHit;
      if (sectionHit) anyVisible = true;
    });

    if (toc) toc.hidden = true;
    noResults.hidden = anyVisible;
    if (!anyVisible) noResults.querySelector("span").textContent = q.trim();
  }

  var t;
  input.addEventListener("input", function () {
    clearTimeout(t);
    t = setTimeout(function () { apply(input.value); }, 60);
  });
  clearBtn.addEventListener("click", function () {
    input.value = "";
    apply("");
    input.focus();
  });
  // Esc でクリア
  input.addEventListener("keydown", function (e) {
    if (e.key === "Escape" && input.value) { input.value = ""; apply(""); }
  });

  // ===== 目次の現在位置ハイライト (sticky サイドバー) =====
  // ビューポート上端の判定線を最後に越えた (= いま読んでいる) セクションを active にする。
  var tocLinks = Array.prototype.slice.call(document.querySelectorAll(".manual-toc a"));
  if (tocLinks.length) {
    var linkById = {};
    tocLinks.forEach(function (a) {
      linkById[a.getAttribute("href").replace(/^#/, "")] = a;
    });
    // sticky ヘッダ + doc-section の scroll-margin-top 相当の判定線 (px)。
    var THRESHOLD = 130;
    var current = null;
    var updateActive = function () {
      if (!sections.length) return;
      var activeId = sections[0].id;
      // sections は文書順 (= 画面の上から下)。上端が判定線以上に来た最後のものを採用。
      for (var i = 0; i < sections.length; i++) {
        if (sections[i].getBoundingClientRect().top <= THRESHOLD) activeId = sections[i].id;
        else break;
      }
      // ページ最下部に到達したら、先頭が判定線まで届かない末尾セクションも active にする。
      if (window.innerHeight + window.scrollY >= document.documentElement.scrollHeight - 2) {
        activeId = sections[sections.length - 1].id;
      }
      if (activeId === current) return;
      current = activeId;
      tocLinks.forEach(function (a) { a.classList.remove("active"); });
      if (linkById[activeId]) linkById[activeId].classList.add("active");
    };
    var ticking = false;
    var onScroll = function () {
      if (ticking) return;
      ticking = true;
      requestAnimationFrame(function () { updateActive(); ticking = false; });
    };
    window.addEventListener("scroll", onScroll, { passive: true });
    window.addEventListener("resize", onScroll);
    updateActive();
  }
})();
