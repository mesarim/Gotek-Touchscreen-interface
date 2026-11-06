// The app's shell (UI unity Task 9): what the device can do (/api/system/info), its skin (/api/skin), navigation (a bar
// below 720 px, a rail from 720), the drive strip, sheets for questions, a toast for what happened, /status every 2 s.
var UI = (function () {
  'use strict';
  var L = AppLogic;
  var ui = { lang: 'en', info: null, status: null, drive: null, features: [], tab: 'drives', views: {}, sub: null };
  var $ = function (id) { return document.getElementById(id); };
  ui.$ = $;
  ui.t = function (k) { return L.t(k, ui.lang, Array.prototype.slice.call(arguments, 1)); };

  ui.esc = function (s) {
    return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
  };
  ui.h = function (html) { var d = document.createElement('div'); d.innerHTML = html; return d.firstElementChild; };

  // fetch as JSON; a refusal's {"error":..} becomes the thrown message
  ui.api = function (url, opt) {
    return fetch(url, opt).then(function (r) {
      return r.text().then(function (txt) {
        var j = null;
        try { j = txt ? JSON.parse(txt) : {}; } catch (e) { j = { error: txt }; }
        if (!r.ok) { var err = new Error((j && (j.error || j.err)) || ('HTTP ' + r.status)); err.code = j && j.code; err.status = r.status; throw err; }
        return j;
      });
    });
  };
  ui.post = function (url) { return ui.api(url, { method: 'POST' }); };
  ui.q = function (o) {
    return Object.keys(o).map(function (k) { return encodeURIComponent(k) + '=' + encodeURIComponent(o[k]); }).join('&');
  };

  var toastTimer = null;
  ui.toast = function (msg, bad) {
    var t = $('toast');
    t.textContent = msg;
    t.className = 'toast on' + (bad ? ' bad' : '');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { t.className = 'toast'; }, 3500);
  };
  ui.fail = function (e) { ui.toast(e && e.code === 'motor' ? ui.t('busy_try') : ui.t('failed', e && e.message || e), true); };

  // A sheet: title, sentence, buttons [{label, cls, fn}]; the safe choice first (left), the action last.
  ui.sheet = function (title, text, buttons, body) {
    var s = $('sheet');
    s.innerHTML = '<h3>' + ui.esc(title) + '</h3>' + (text ? '<p>' + ui.esc(text) + '</p>' : '');
    if (body) s.appendChild(body);
    var row = ui.h('<div class="btns"></div>');
    buttons.forEach(function (b) {
      var el = ui.h('<button class="btn ' + (b.cls || '') + '">' + ui.esc(b.label) + '</button>');
      el.onclick = function () { if (!b.keep) ui.close(); if (b.fn) b.fn(); };
      row.appendChild(el);
    });
    s.appendChild(row);
    $('veil').hidden = false;
  };
  ui.close = function () { $('veil').hidden = true; };
  // Before a load into drive (0 or 1): asked first when what the Amiga wrote to the disk there would be lost.
  ui.beforeLoad = function (drive, fn) {
    if (!L.replaceWarning(ui.status, drive)) { fn(); return; }
    ui.confirm(ui.t('lose_q', 'DF' + drive), ui.t('lose_s'), ui.t('replace'), fn);
  };
  ui.confirm = function (title, text, label, fn) {
    ui.sheet(title, text, [{ label: ui.t('cancel') }, { label: label, cls: 'danger', fn: fn }]);
  };
  $('veil').onclick = function (e) { if (e.target === $('veil')) ui.close(); };

  var ICON = {
    drives: '<path d="M5 3h11l3 3v15H5z M8 3v5h7V3 M8 14h8v7H8z" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linejoin="round"/>',
    games: '<path d="M4 4h7v7H4zM13 4h7v7h-7zM4 13h7v7H4zM13 13h7v7h-7z" fill="none" stroke="currentColor" stroke-width="1.6"/>',
    files: '<path d="M3 6h7l2 2h9v11H3z" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linejoin="round"/>',
    webdav: '<path d="M7 18h10a4 4 0 0 0 .5-8 6 6 0 0 0-11.5 1A3.5 3.5 0 0 0 7 18z" fill="none" stroke="currentColor" stroke-width="1.6"/>',
    more: '<circle cx="5" cy="12" r="1.8" fill="currentColor"/><circle cx="12" cy="12" r="1.8" fill="currentColor"/><circle cx="19" cy="12" r="1.8" fill="currentColor"/>'
  };

  function renderNav() {
    var tabs = L.tabsFor(ui.features);
    if (tabs.indexOf(ui.tab) < 0) ui.tab = tabs[0];
    ['rail', 'tabbar'].forEach(function (id) {
      var nav = $(id);
      nav.innerHTML = '';
      tabs.forEach(function (tab) {
        var b = ui.h('<button class="' + (tab === ui.tab ? 'on' : '') + '"><svg viewBox="0 0 24 24">' + ICON[tab] +
          '</svg><span>' + ui.esc(ui.t('tab_' + tab)) + '</span></button>');
        b.onclick = function () { ui.go(tab); };
        nav.appendChild(b);
      });
    });
  }

  ui.go = function (tab, sub) {
    ui.tab = tab;
    ui.sub = sub || null;
    try { localStorage.setItem('fd_tab', tab); } catch (e) { /* private window */ }
    renderNav();
    ui.render();
    window.scrollTo(0, 0);
  };
  ui.render = function () {
    var v = ui.views[ui.tab];
    var view = $('view');
    view.innerHTML = '';
    if (v) v(view, ui.sub);
  };

  function renderHeader() {
    var s = ui.status || {};
    $('dev').textContent = (ui.info && ui.info.device) || 'FluxDrive';
    document.title = $('dev').textContent;
    var parts = [s.devname || '', 'amiga' in s ? (s.amiga ? ui.t('amiga_on') : ui.t('amiga_off')) : ''];   // a dongle cannot see the Amiga
    if (ui.drive && ui.drive.motor) parts.push(ui.t('amiga_busy'));
    $('stat').textContent = parts.filter(Boolean).join(' · ');
    $('dot').className = 'dot' + (s.ip ? ' on' : '');
  }

  // Every drive this device reaches (FluxDrive: DF0 and DF1), small, at the bottom of every tab.
  ui.renderStrip = function () {
    var strip = $('strip');
    strip.innerHTML = '';
    ui.driveCards().forEach(function (c) {
      var d = ui.h('<div class="drv' + (c.loaded ? ' in' : '') + (c.dimmed ? ' off' : '') + '"><span><b>' + c.id + '</b>' +
        ui.esc(c.name) + '</span><span class="dim">' + ui.esc(c.line) + '</span></div>');
      d.onclick = function () { ui.go('drives'); };
      strip.appendChild(d);
    });
  };
  ui.driveCards = function () { return []; };   // Task 10 (ui_1_drives.js)

  ui.refresh = function () {
    return Promise.all([ui.api('/status'), ui.api('/api/drive').catch(function () { return null; })]).then(function (r) {
      ui.status = r[0];
      ui.drive = r[1];
      // a stick in or out: its tabs and actions come and go without a reload, Games reads it again
      var f = L.withStick(ui.features, !!(ui.status.usb && ui.status.usb.state === 'ready'));
      if (f !== ui.features) {
        ui.features = f;
        ui.games = null;
        renderNav();
        if ($('veil').hidden) ui.render();
      }
      renderHeader();
      ui.renderStrip();
      var v = ui.views[ui.tab];
      if (v && v.live && $('veil').hidden) ui.render();   // a view that shows the drives follows them
      else if (v && v.redraw) v.redraw();                  // or redraws the part that does
    }).catch(function () { $('dot').className = 'dot'; });
  };

  // Favourites, kept on the FluxDrive (/api/favs): a set of keys (AppLogic.favKey), and a star button for any of them.
  ui.favs = {};
  ui.loadFavs = function () {
    return ui.api('/api/favs').then(function (f) { ui.favs = {}; (f.favs || []).forEach(function (k) { ui.favs[k] = 1; }); }).catch(function () {});
  };
  ui.isFav = function (key) { return !!ui.favs[key]; };
  ui.star = function (key) {
    var b = ui.h('<button class="star' + (ui.isFav(key) ? ' on' : '') + '" aria-label="' + ui.esc(ui.t('fav')) + '">' +
      (ui.isFav(key) ? '★' : '☆') + '</button>');
    b.onclick = function (e) {
      e.stopPropagation();
      var on = !ui.isFav(key);
      ui.post('/api/favs?' + ui.q({ path: key, on: on ? 1 : 0 })).then(function (f) {
        ui.favs = {};
        (f.favs || []).forEach(function (k) { ui.favs[k] = 1; });
        b.className = 'star' + (on ? ' on' : '');
        b.textContent = on ? '★' : '☆';
      }).catch(ui.fail);
    };
    return b;
  };

  ui.applySkin = function (tokens) { L.applySkin(tokens, document.documentElement.style); };

  ui.start = function () {
    try { var saved = localStorage.getItem('fd_skin'); if (saved) ui.applySkin(JSON.parse(saved)); } catch (e) { /* none yet */ }
    ui.api('/api/skin').then(function (s) {
      ui.applySkin(s.tokens);
      try { localStorage.setItem('fd_skin', JSON.stringify(s.tokens)); } catch (e) { /* private window */ }
    }).catch(function () {});
    ui.api('/api/system/info').then(function (i) {
      ui.info = i;
      ui.features = i.features || ['drives'];
      ui.lang = i.lang === 'nl' ? 'nl' : 'en';
      document.documentElement.lang = ui.lang;
      var last = null;
      try { last = localStorage.getItem('fd_tab'); } catch (e) { /* private window */ }
      ui.tab = last || 'drives';
      renderNav();
      ui.loadFavs().then(function () { ui.render(); });
      ui.render();
      ui.refresh();
      setInterval(ui.refresh, 2000);
    }).catch(function () { $('stat').textContent = 'offline'; });
  };
  return ui;
})();
