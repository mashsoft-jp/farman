(function () {
  "use strict";

  function initScreenshotDialog() {
    if (typeof HTMLDialogElement === "undefined") return;

    var shots = document.querySelectorAll(".hero-shot img, .shot-grid img, .doc-figure img");
    if (!shots.length) return;

    var dialog = document.createElement("dialog");
    dialog.className = "shot-dialog";
    dialog.setAttribute("aria-label", "Screenshot preview");

    var close = document.createElement("button");
    close.type = "button";
    close.setAttribute("aria-label", "Close");
    close.textContent = "×";

    var image = document.createElement("img");
    image.alt = "";

    dialog.appendChild(close);
    dialog.appendChild(image);
    document.body.appendChild(dialog);

    function closeDialog() {
      dialog.close();
    }

    close.addEventListener("click", closeDialog);
    dialog.addEventListener("click", function (event) {
      if (event.target === dialog) closeDialog();
    });

    shots.forEach(function (shot) {
      shot.addEventListener("click", function () {
        image.src = shot.currentSrc || shot.src;
        image.alt = shot.alt || "";
        dialog.showModal();
        dialog.scrollTo(0, 0);
      });
    });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initScreenshotDialog);
  } else {
    initScreenshotDialog();
  }
})();

/* ===== プラグインの最新リリース情報と "New" バッジ =====
 *
 * 各プラグインの最新リリースを GitHub Releases API から取得し、
 *   - ヘッダの「プラグイン」リンク (全ページ共通): どれか 1 つでも過去 1 か月以内に
 *     公開されたプラグインがあれば "New" バッジを付ける
 *   - プラグインページの各カード: plugins.js が同じデータで OS 別ダウンロードと
 *     カード見出しの "New" バッジを描く
 * リリース日は API の published_at を使うので、HTML に日付を書く必要はない
 * (プラグインをリリースすれば自動で New になり、1 か月経てば自動で消える)。
 *
 * API は 60 req/h/IP のレート制限があるため、取得結果を localStorage に 1 時間
 * キャッシュしてページ遷移ごとの再取得を避ける。取得に失敗した場合はバッジを
 * 出さないだけで、他の表示には影響しない。
 *
 * プラグインを追加したら PLUGIN_REPOS に owner/repo を足す (プラグインページの
 * カードの data-plugin-repo と同じ値)。
 */
(function () {
  "use strict";

  var PLUGIN_REPOS = [
    "mashsoft-jp/farman-plugin-3d",
    "mashsoft-jp/farman-plugin-lzh"
  ];
  var CACHE_KEY = "farman.pluginReleases.v1";
  var CACHE_TTL_MS = 60 * 60 * 1000;  // 1 時間
  var NEW_MONTHS = 1;                  // 「過去 1 か月以内」を New とみなす

  function readCache() {
    try {
      var raw = localStorage.getItem(CACHE_KEY);
      if (!raw) return {};
      var obj = JSON.parse(raw);
      return (obj && typeof obj === "object") ? obj : {};
    } catch (e) { return {}; }
  }
  function writeCache(obj) {
    try { localStorage.setItem(CACHE_KEY, JSON.stringify(obj)); } catch (e) { /* 容量超過や無効化は無視 */ }
  }

  // 1 リポジトリ分を取得 (キャッシュが新しければそれを返す)。失敗時は null。
  function fetchOne(repo, cache, now) {
    var hit = cache[repo];
    if (hit && (now - hit.fetchedAt) < CACHE_TTL_MS) return Promise.resolve(hit.data);
    return fetch("https://api.github.com/repos/" + repo + "/releases/latest",
                 { headers: { "Accept": "application/vnd.github+json" } })
      .then(function (res) { if (!res.ok) throw new Error("HTTP " + res.status); return res.json(); })
      .then(function (json) {
        // 必要な項目だけ残してキャッシュを小さく保つ。
        var data = {
          tag_name: json.tag_name || "",
          published_at: json.published_at || "",
          assets: (json.assets || []).map(function (a) {
            return { name: a.name, browser_download_url: a.browser_download_url };
          })
        };
        cache[repo] = { fetchedAt: now, data: data };
        writeCache(cache);
        return data;
      })
      .catch(function (err) {
        if (window.console) console.warn("farman: plugin release fetch failed:", repo, err);
        return null;
      });
  }

  // 全プラグインの最新リリースを { repo: data | null } で返す Promise。
  // 同一ページ内では 1 回だけ取得し、以後は同じ Promise を返す。
  var pending = null;
  function pluginReleases() {
    if (pending) return pending;
    var cache = readCache();
    var now = Date.now();
    pending = Promise.all(PLUGIN_REPOS.map(function (repo) { return fetchOne(repo, cache, now); }))
      .then(function (list) {
        var out = {};
        PLUGIN_REPOS.forEach(function (repo, i) { out[repo] = list[i]; });
        return out;
      });
    return pending;
  }

  // 公開日が「過去 NEW_MONTHS か月以内」なら true。
  function isNewRelease(publishedAt) {
    if (!publishedAt) return false;
    var t = new Date(publishedAt).getTime();
    if (isNaN(t)) return false;
    var cutoff = new Date();
    cutoff.setMonth(cutoff.getMonth() - NEW_MONTHS);
    return t >= cutoff.getTime();
  }

  function makeBadge() {
    var b = document.createElement("span");
    b.className = "badge-new";
    b.textContent = "New";
    return b;
  }

  // plugins.js から使う共有 API。
  window.farmanPluginReleases = pluginReleases;
  window.farmanIsNewRelease = isNewRelease;
  window.farmanMakeNewBadge = makeBadge;

  // ヘッダの「プラグイン」リンクに New バッジ (全ページ共通)。
  function initHeaderBadge() {
    // 言語切替リンク (English / 日本語) も href が plugins/ で終わるので除外する。
    var links = document.querySelectorAll('.site-header .nav a:not(.lang-switch)[href$="plugins/"]');
    if (!links.length) return;
    pluginReleases().then(function (map) {
      var anyNew = PLUGIN_REPOS.some(function (repo) {
        return map[repo] && isNewRelease(map[repo].published_at);
      });
      if (!anyNew) return;
      Array.prototype.forEach.call(links, function (a) { a.appendChild(makeBadge()); });
    });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initHeaderBadge);
  } else {
    initHeaderBadge();
  }
})();
