/* ===== farman download buttons (GitHub Releases API) =====
 *
 * リリースのたびにサイトを更新しなくて済むよう、最新の安定版リリースを
 * GitHub API から取得して各 OS のダウンロードボタンを動的に生成する。
 * アセット名にバージョンが入る (farman-vX.Y.Z-macos-arm64.dmg など) ため
 * 固定の直リンクが作れないので、この方式を採る。
 *
 * 日本語版 (/) と英語版 (/en/) で共用。文言は <html lang> を見て出し分ける。
 * API 取得に失敗した場合は GitHub Releases ページへのリンクにフォールバック。
 */
(function () {
  "use strict";

  var REPO = "mashsoft-jp/farman";
  var API = "https://api.github.com/repos/" + REPO + "/releases/latest";
  var RELEASES_PAGE = "https://github.com/" + REPO + "/releases/latest";

  // <html lang="en"> なら英語、それ以外は日本語。
  var LANG = ((document.documentElement.lang || "ja").toLowerCase().indexOf("en") === 0)
    ? "en" : "ja";

  var STR = {
    ja: {
      latest: "最新リリース: ",
      changelog: "変更履歴",
      download: "ダウンロード",
      heroMac: "macOS 版をダウンロード",
      heroWin: "Windows 版をダウンロード",
      heroLinux: "Linux 版をダウンロード",
      dmg: "ダウンロード (.dmg)",
      exe: "インストーラ (.exe)",
      zip: "ポータブル版 (.zip)",
      appimage: "AppImage",
      deb: "Debian / Ubuntu (.deb)",
      releasesPage: "リリースページへ",
      allOS: "すべての OS",
      openReleases: "リリースページを開く",
      fetchFail: "リリース情報を取得できませんでした。",
      fromReleases: ' <a href="' + RELEASES_PAGE + '" rel="noopener">GitHub Releases</a> から直接ダウンロードしてください。'
    },
    en: {
      latest: "Latest release: ",
      changelog: "Changelog",
      download: "Download",
      heroMac: "Download for macOS",
      heroWin: "Download for Windows",
      heroLinux: "Download for Linux",
      dmg: "Download (.dmg)",
      exe: "Installer (.exe)",
      zip: "Portable (.zip)",
      appimage: "AppImage",
      deb: "Debian / Ubuntu (.deb)",
      releasesPage: "Releases page",
      allOS: "All platforms",
      openReleases: "Open releases page",
      fetchFail: "Could not fetch release info.",
      fromReleases: ' Download directly from <a href="' + RELEASES_PAGE + '" rel="noopener">GitHub Releases</a>.'
    }
  };
  var L = STR[LANG];

  // 訪問者の OS を推定 (macos / windows / linux / unknown)。
  function detectOS() {
    var ua = navigator.userAgent || "";
    var plat = navigator.platform || "";
    if (/Mac/i.test(ua) || /Mac/i.test(plat)) return "macos";
    if (/Win/i.test(ua) || /Win/i.test(plat)) return "windows";
    if (/Linux|X11|Android/i.test(ua)) return "linux";
    return "unknown";
  }

  // アセット配列から OS / 形式ごとのダウンロード URL を拾う。
  function pickAssets(assets) {
    function find(pred) {
      for (var i = 0; i < assets.length; i++) {
        var n = assets[i].name.toLowerCase();
        if (n.endsWith(".sha256")) continue; // チェックサムは除外
        if (pred(n)) return assets[i].browser_download_url;
      }
      return null;
    }
    return {
      macos:         find(function (n) { return n.indexOf("macos") >= 0 && n.endsWith(".dmg"); }),
      windowsSetup:  find(function (n) { return n.indexOf("windows") >= 0 && n.endsWith("-setup.exe"); }),
      windowsZip:    find(function (n) { return n.indexOf("windows") >= 0 && n.endsWith(".zip"); }),
      linuxAppImage: find(function (n) { return n.indexOf("linux") >= 0 && n.endsWith(".appimage"); }),
      linuxDeb:      find(function (n) { return n.indexOf("linux") >= 0 && n.endsWith(".deb"); })
    };
  }

  // OS カードの定義。primary = 主たるインストーラ、secondary = 補助形式。
  function buildCards(urls, currentOS) {
    return [
      {
        os: "macos",
        title: "macOS",
        arch: "Apple Silicon (arm64)",
        primary: urls.macos ? { label: L.dmg, url: urls.macos } : null,
        secondary: null
      },
      {
        os: "windows",
        title: "Windows",
        arch: "64-bit (x64)",
        primary: urls.windowsSetup ? { label: L.exe, url: urls.windowsSetup } : null,
        secondary: urls.windowsZip ? { label: L.zip, url: urls.windowsZip } : null
      },
      {
        os: "linux",
        title: "Linux",
        arch: "x86_64",
        primary: urls.linuxAppImage ? { label: L.appimage, url: urls.linuxAppImage } : null,
        secondary: urls.linuxDeb ? { label: L.deb, url: urls.linuxDeb } : null
      }
    ].map(function (c) { c.isCurrent = (c.os === currentOS); return c; });
  }

  function renderCards(cards) {
    var grid = document.getElementById("download-grid");
    if (!grid) return;
    grid.innerHTML = "";

    // 自 OS を先頭に並べ替え (見つけやすく)。
    cards.sort(function (a, b) { return (b.isCurrent ? 1 : 0) - (a.isCurrent ? 1 : 0); });

    cards.forEach(function (c) {
      var card = document.createElement("div");
      card.className = "dl-card" + (c.isCurrent ? " is-current" : "");

      var html = '<p class="dl-os">' + c.title + "</p>"
               + '<p class="dl-arch">' + c.arch + "</p>";

      if (c.primary) {
        html += '<a class="btn btn-primary" href="' + c.primary.url + '">'
              + c.primary.label + "</a>";
      } else {
        html += '<a class="btn btn-ghost" href="' + RELEASES_PAGE + '" rel="noopener">'
              + L.releasesPage + "</a>";
      }
      if (c.secondary) {
        html += '<a class="dl-secondary" href="' + c.secondary.url + '">'
              + c.secondary.label + "</a>";
      }
      if (c.note) {
        html += '<p class="dl-note">' + c.note + "</p>";
      }
      card.innerHTML = html;
      grid.appendChild(card);
    });
  }

  // 自 OS の主たるアセットをヒーローの大ボタンに反映。
  function updateHeroButton(urls, currentOS) {
    var btn = document.getElementById("hero-download");
    if (!btn) return;
    var map = {
      macos:   { url: urls.macos,         label: L.heroMac },
      windows: { url: urls.windowsSetup,  label: L.heroWin },
      linux:   { url: urls.linuxAppImage, label: L.heroLinux }
    };
    var pick = map[currentOS];
    if (pick && pick.url) {
      btn.href = pick.url;
      btn.textContent = pick.label;
    } else {
      btn.href = "#download";
      btn.textContent = L.download;
    }
  }

  function setVersionText(tag, publishedAt) {
    var date = "";
    if (publishedAt) {
      var d = new Date(publishedAt);
      if (!isNaN(d)) {
        date = " · " + d.getFullYear() + "-"
             + ("0" + (d.getMonth() + 1)).slice(-2) + "-"
             + ("0" + d.getDate()).slice(-2);
      }
    }
    var heroV = document.getElementById("hero-version");
    var dlV = document.getElementById("download-version");
    if (heroV) {
      heroV.textContent = L.latest + tag + date + " · ";
      var cl = document.createElement("a");
      cl.href = RELEASES_PAGE;
      cl.rel = "noopener";
      cl.textContent = L.changelog;
      heroV.appendChild(cl);
    }
    if (dlV) dlV.textContent = L.latest + tag + date;
  }

  function fallback() {
    var heroV = document.getElementById("hero-version");
    var dlV = document.getElementById("download-version");
    if (heroV) heroV.textContent = "";
    if (dlV) dlV.innerHTML = L.fetchFail + L.fromReleases;
    var grid = document.getElementById("download-grid");
    if (grid && !grid.children.length) {
      grid.innerHTML = '<div class="dl-card"><p class="dl-os">' + L.allOS + "</p>"
        + '<p class="dl-arch">macOS / Windows / Linux</p>'
        + '<a class="btn btn-primary" href="' + RELEASES_PAGE + '" rel="noopener">'
        + L.openReleases + "</a></div>";
    }
  }

  function init() {
    var os = detectOS();
    fetch(API, { headers: { "Accept": "application/vnd.github+json" } })
      .then(function (res) {
        if (!res.ok) throw new Error("HTTP " + res.status);
        return res.json();
      })
      .then(function (data) {
        if (!data || !data.assets || !data.assets.length) throw new Error("no assets");
        var urls = pickAssets(data.assets);
        setVersionText(data.tag_name || "latest", data.published_at);
        updateHeroButton(urls, os);
        renderCards(buildCards(urls, os));
      })
      .catch(function (err) {
        // レート制限 (60 req/h/IP) やオフライン時はフォールバック。
        fallback();
        if (window.console) console.warn("farman: release fetch failed:", err);
      });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
