// SPDX-License-Identifier: MPL-2.0
// STARFISH: one shared tab row (Install / User guide / Emulator / Download firmware) for the installer, the user
// guide and the emulator. The workflow adds <script src="starfish-nav.js" defer></script> to those three pages.
//  - Installer and guide: the row uses their own colour tokens; their light-blue cross-link button and the language
//    pill are hidden (the row replaces them).
//  - Emulator: takes the installer's palette and its saved light/dark choice, and the same page width, so the row sits
//    exactly where it does on the installer.
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
  var isEmu = current === "emu";
  var FIRMWARE = "https://github.com/Grandsummoner/starfish/releases/download/latest/starfish.fwsc";
  var THEME_KEYS = ["starfish-theme", "starfish-guide-theme"];   // the installer and the guide share these

  // ---- the installer's palette, for the emulator (index.html :root and :root[data-theme="light"]) ----
  var PALETTE = {
    dark: { bg: "#0b0e14", surface: "#121722", surface2: "#182030", border: "#1e293b", fg: "#eef2f7", muted: "#94a3b8",
            accent: "#22d3ee", onAccent: "#071018", good: "#34d399", bad: "#fb7185", focus: "#38bdf8" },
    light: { bg: "#f6f7f9", surface: "#ffffff", surface2: "#eef2f6", border: "#d8dee9", fg: "#0f172a", muted: "#475569",
             accent: "#0284c7", onAccent: "#ffffff", good: "#047857", bad: "#dc2626", focus: "#0369a1" }
  };
  function savedTheme() {
    try {
      for (var k = 0; k < THEME_KEYS.length; k++) {
        var v = localStorage.getItem(THEME_KEYS[k]);
        if (v === "light" || v === "dark") return v;
      }
    } catch (e) { /* storage blocked */ }
    return "dark";
  }
  function applyEmuTheme(mode) {
    var p = PALETTE[mode], s = document.documentElement.style;
    var vars = {
      "--bg": p.bg, "--surface": p.surface, "--surface-2": p.surface2, "--border": p.border, "--fg": p.fg,
      "--muted": p.muted, "--accent": p.accent, "--on-accent": p.onAccent, "--focus": p.focus,
      // the emulator's own names
      "--ground": p.bg, "--ink": p.fg, "--rule": p.border, "--card": p.surface, "--teal": p.good, "--bad": p.bad
    };
    for (var name in vars) s.setProperty(name, vars[name]);
    s.colorScheme = mode;
    document.documentElement.setAttribute("data-theme", mode);
  }

  var css = document.createElement("style");
  css.id = "sf-nav-css";
  css.textContent =
    // the row: same width and side padding as the installer's top bar, so it lines up on every page
    "#sf-nav{box-sizing:border-box;max-width:1180px;margin:0 auto;padding:14px 24px 0;display:flex;flex-wrap:wrap;" +
    "gap:8px;align-items:center;font:600 13px/1.2 'Plus Jakarta Sans','DM Sans',-apple-system,BlinkMacSystemFont," +
    "'Segoe UI',system-ui,sans-serif}" +
    "#sf-nav a{display:inline-flex;align-items:center;padding:8px 16px;border-radius:999px;" +
    "border:1px solid var(--border,#1e293b);background:var(--surface,#121722);color:var(--fg,#eef2f7);" +
    "text-decoration:none;white-space:nowrap;transition:border-color .15s ease}" +
    "#sf-nav a:hover{border-color:var(--accent,#22d3ee);text-decoration:none}" +
    "#sf-nav a:focus-visible{outline:2px solid var(--focus,#38bdf8);outline-offset:2px}" +
    "#sf-nav a[aria-current='page']{background:var(--fg,#eef2f7);color:var(--bg,#0b0e14);border-color:transparent}" +
    "#sf-nav .sf-dl{margin-left:auto}" +
    "@media (max-width:520px){#sf-nav .sf-dl{margin-left:0}}";
  if (isEmu) {
    css.textContent +=
      // the emulator on the installer's page width, with the installer's accent on its start button
      "body{padding-inline:0;padding-block-start:0}" +
      "main,body>header{max-width:1132px;padding-inline:24px}" +
      "body>header{margin-top:22px}" +
      ".start button{background:var(--accent);color:var(--on-accent)}";
  } else {
    // the row replaces the light-blue cross-link button and the language pill in the page's own top bar
    css.textContent += "header.topbar .top-cta,header.topbar #lang{display:none!important}";
  }
  document.head.appendChild(css);

  if (isEmu) applyEmuTheme(savedTheme());

  var items = [
    ["install", "Install", base + "index.html"],
    ["guide", "User guide", base + "guide.html"],
    ["emu", "Emulator", base + "emu/"]
  ];
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

  // the emulator has no theme button of its own: one in its button row, saved where the installer keeps its choice
  if (isEmu) {
    var bar = document.querySelector(".bar");
    if (bar) {
      var tb = document.createElement("button");
      tb.type = "button";
      tb.id = "sf-theme";
      var mode = savedTheme();
      var label = function () { tb.textContent = mode === "light" ? "Dark theme" : "Light theme"; };
      label();
      tb.addEventListener("click", function () {
        mode = mode === "light" ? "dark" : "light";
        try { THEME_KEYS.forEach(function (k) { localStorage.setItem(k, mode); }); } catch (e) { /* storage blocked */ }
        applyEmuTheme(mode);
        label();
      });
      bar.appendChild(tb);
    }
  }
})();
