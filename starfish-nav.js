// SPDX-License-Identifier: MPL-2.0
// STARFISH: one shared pill bar for the installer, the user guide and the emulator.
// The workflow adds <script src="starfish-nav.js" defer></script> to those three published pages. The bar takes its
// text and background colours from the page it sits in, so it follows each page's own light/dark theme.
(function () {
  "use strict";
  if (document.getElementById("sf-nav")) return;

  // the folder this script was loaded from is the site's root
  var me = document.currentScript;
  if (!me) {
    var all = document.getElementsByTagName("script");
    for (var i = 0; i < all.length; i++) if (/starfish-nav\.js(\?|$)/.test(all[i].src)) me = all[i];
  }
  if (!me || !me.src) return;
  var base = me.src.replace(/[?#].*$/, "").replace(/[^\/]*$/, "");

  var path = location.pathname;
  var current = /\/guide\.html$/.test(path) ? "guide" : /\/emu(\/|\/index\.html)?$/.test(path) ? "emu" : "install";
  var items = [
    ["install", "Install", base + "index.html"],
    ["guide", "User guide", base + "guide.html"],
    ["emu", "Emulator", base + "emu/"]
  ];
  var FIRMWARE = "https://github.com/Grandsummoner/starfish/releases/download/latest/starfish.fwsc";

  var css = document.createElement("style");
  css.id = "sf-nav-css";
  css.textContent =
    "#sf-nav{--sf-fg:#222;--sf-bg:#fff;box-sizing:border-box;max-width:1100px;margin:0 auto;padding:10px 16px;" +
    "display:flex;flex-wrap:wrap;gap:8px;align-items:center;" +
    "font:600 14px/1.2 system-ui,-apple-system,'Segoe UI',Roboto,Arial,sans-serif}" +
    "#sf-nav a{color:var(--sf-fg);text-decoration:none;padding:.55em 1.1em;border-radius:999px;" +
    "border:1px solid rgba(128,128,128,.5);white-space:nowrap}" +
    "#sf-nav a:hover{background:rgba(128,128,128,.18)}" +
    "#sf-nav a:focus-visible{outline:2px solid var(--sf-fg);outline-offset:2px}" +
    "#sf-nav a[aria-current='page']{background:var(--sf-fg);color:var(--sf-bg);border-color:transparent}" +
    "#sf-nav .sf-dl{margin-left:auto}" +
    "@media (max-width:520px){#sf-nav .sf-dl{margin-left:0}}";
  document.head.appendChild(css);

  var nav = document.createElement("nav");
  nav.id = "sf-nav";
  nav.setAttribute("aria-label", "STARFISH");
  items.forEach(function (it) {
    var a = document.createElement("a");
    a.href = it[2];
    a.textContent = it[1];
    if (it[0] === current) a.setAttribute("aria-current", "page");
    nav.appendChild(a);
  });
  var dl = document.createElement("a");
  dl.className = "sf-dl";
  dl.href = FIRMWARE;
  dl.rel = "noopener";
  dl.textContent = "Download firmware";
  nav.appendChild(dl);
  document.body.insertBefore(nav, document.body.firstChild);

  // follow the page's own colours (and its theme switch, if it has one)
  function clear(c) { return !c || c === "transparent" || c === "rgba(0, 0, 0, 0)"; }
  function sync() {
    var cs = getComputedStyle(document.body);
    var bg = cs.backgroundColor;
    if (clear(bg)) bg = getComputedStyle(document.documentElement).backgroundColor;
    if (clear(bg)) bg = "Canvas";
    nav.style.setProperty("--sf-fg", cs.color);
    nav.style.setProperty("--sf-bg", bg);
  }
  sync();
  try {
    var mo = new MutationObserver(sync);
    mo.observe(document.documentElement, { attributes: true });
    mo.observe(document.body, { attributes: true });
  } catch (e) { /* no observer: the colours stay as first read */ }
  if (window.matchMedia) {
    var mq = window.matchMedia("(prefers-color-scheme: dark)");
    if (mq.addEventListener) mq.addEventListener("change", sync);
  }
  window.addEventListener("load", sync);
})();
