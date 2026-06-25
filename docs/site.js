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
