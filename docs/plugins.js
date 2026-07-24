/* ===== farman プラグインの OS 別ダウンロードボタン (GitHub Releases API) =====
 *
 * 本体の download.js と同様に、各プラグインの最新リリースを GitHub API から
 * 取得して OS 別 (.dylib / .dll / .so) のダウンロードリンクを生成する。
 * アセット名にバージョンが入る (LzhArchivePlugin-vX.Y.Z-macos-arm64.dylib など)
 * ため固定の直リンクが作れないので、本体と同じくこの方式を採る。
 *
 * data-plugin-repo="owner/repo" を持つカードごとに処理する。
 * API 取得に失敗した場合 (レート制限・オフライン) や JS 無効時は、カード内に
 * 元からあるフォールバックリンク (リリースページ + リポジトリ) をそのまま残す。
 * 日本語版 (/plugins/) と英語版 (/en/plugins/) で共用。<html lang> で文言を出し分け。
 */
(function () {
  "use strict";

  var LANG = ((document.documentElement.lang || "ja").toLowerCase().indexOf("en") === 0)
    ? "en" : "ja";

  var STR = {
    ja: { macos: "macOS (.dylib)", windows: "Windows (.dll)", linux: "Linux (.so)",
          repo: "リポジトリ", latest: "最新: " },
    en: { macos: "macOS (.dylib)", windows: "Windows (.dll)", linux: "Linux (.so)",
          repo: "Repository", latest: "Latest: " }
  };
  var L = STR[LANG];

  // 訪問者の OS を推定 (macos / windows / linux / unknown)。
  function detectOS() {
    var ua = navigator.userAgent || "", p = navigator.platform || "";
    if (/Mac/i.test(ua) || /Mac/i.test(p)) return "macos";
    if (/Win/i.test(ua) || /Win/i.test(p)) return "windows";
    if (/Linux|X11|Android/i.test(ua)) return "linux";
    return "unknown";
  }

  // アセット配列から OS ごとのダウンロード URL を拾う (.sha256 は除外)。
  function pickAssets(assets) {
    function find(pred) {
      for (var i = 0; i < assets.length; i++) {
        var n = assets[i].name.toLowerCase();
        if (n.slice(-7) === ".sha256") continue;
        if (pred(n)) return assets[i].browser_download_url;
      }
      return null;
    }
    return {
      macos:   find(function (n) { return n.indexOf("macos") >= 0   && n.slice(-6) === ".dylib"; }),
      windows: find(function (n) { return n.indexOf("windows") >= 0 && n.slice(-4) === ".dll"; }),
      linux:   find(function (n) { return n.indexOf("linux") >= 0   && n.slice(-3) === ".so"; })
    };
  }

  var os = detectOS();

  function render(card, data) {
    var slot = card.querySelector(".plugin-dl");
    if (!slot) return;
    var urls = pickAssets(data.assets || []);
    if (!urls.macos && !urls.windows && !urls.linux) return; // 取れなければフォールバックのまま

    // 訪問者の OS を先頭に並べ替え、その OS を主ボタン (primary) にする。
    var order = ["macos", "windows", "linux"].sort(function (a, b) {
      return (b === os ? 1 : 0) - (a === os ? 1 : 0);
    });

    var html = "";
    order.forEach(function (k) {
      if (!urls[k]) return;
      var cls = (k === os) ? "btn btn-primary btn-sm" : "btn btn-ghost btn-sm";
      html += '<a class="' + cls + '" href="' + urls[k] + '">' + L[k] + "</a>\n";
    });
    html += '<a class="btn btn-ghost btn-sm" href="https://github.com/'
          + card.getAttribute("data-plugin-repo") + '" rel="noopener">' + L.repo + "</a>";
    if (data.tag_name) {
      html += ' <span class="plugin-ver">' + L.latest + data.tag_name + "</span>";
    }
    slot.innerHTML = html;
  }

  var cards = document.querySelectorAll("[data-plugin-repo]");
  Array.prototype.forEach.call(cards, function (card) {
    var repo = card.getAttribute("data-plugin-repo");
    fetch("https://api.github.com/repos/" + repo + "/releases/latest",
          { headers: { "Accept": "application/vnd.github+json" } })
      .then(function (res) { if (!res.ok) throw new Error("HTTP " + res.status); return res.json(); })
      .then(function (data) { render(card, data); })
      .catch(function (err) {
        // レート制限 (60 req/h/IP) やオフライン時はフォールバックリンクを残す。
        if (window.console) console.warn("farman: plugin release fetch failed:", repo, err);
      });
  });
})();
