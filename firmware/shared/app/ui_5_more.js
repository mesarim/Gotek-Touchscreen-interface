// More (UI unity Task 12): settings (language, skin, WebDAV, home Wi-Fi), system (firmware, restart) and the fleet.
// A setting is a row with a value or a switch; an action is a button; a restart is asked first.
(function (ui) {
  'use strict';
  var L = AppLogic;

  function row(label, value, fn) {
    var r = ui.h('<div class="row' + (fn ? '' : ' set') + '"><div><span class="t">' + ui.esc(label) + '</span></div><span class="v">' +
      ui.esc(value || '') + (fn ? ' ›' : '') + '</span></div>');
    if (fn) r.onclick = fn;
    return r;
  }
  function field(label, type, value, ph) {
    var w = ui.h('<label style="display:block;margin-bottom:8px"><span class="dim" style="font-size:var(--fs-s)">' + ui.esc(label) +
      '</span><input class="input" type="' + type + '"></label>');
    var i = w.querySelector('input');
    i.value = value == null ? '' : value;
    if (ph) i.placeholder = ph;
    return { el: w, input: i };
  }
  function back(view, title) {
    var top = ui.h('<div class="bar2"><button class="btn">‹ ' + ui.esc(ui.t('tab_more')) + '</button><h2>' + ui.esc(title) + '</h2></div>');
    top.querySelector('button').onclick = function () { ui.go('more'); };
    view.appendChild(top);
  }

  function skinPage(view) {
    back(view, ui.t('skin'));
    var cur = ui.h('<div class="lbl"></div>');
    view.appendChild(cur);
    ui.api('/api/skin').then(function (s) { cur.textContent = s.name; });
    function choose(name, tokens) {
      ui.api('/api/skin', { method: 'POST', body: JSON.stringify({ name: name, tokens: tokens }) }).then(function (s) {
        ui.applySkin(s.tokens);
        try { localStorage.setItem('fd_skin', JSON.stringify(s.tokens)); } catch (e) { /* private window */ }
        cur.textContent = s.name;
        ui.toast(ui.t('saved'));
      }).catch(ui.fail);
    }
    L.skinPresets().forEach(function (p) {
      var r = ui.h('<div class="row"><div><span class="t">' + ui.esc(p.name) + '</span></div>' +
        '<span style="display:flex;gap:4px"></span></div>');
      var sw = r.lastChild;
      ['card', 'bar', 'accent', 'txt'].forEach(function (k) {
        var c = document.createElement('i');
        c.style.cssText = 'width:18px;height:18px;border-radius:4px;border:1px solid var(--line)';
        c.style.background = p.tokens[k];
        sw.appendChild(c);
      });
      r.onclick = function () { choose(p.name, p.tokens); };
      view.appendChild(r);
    });
    var pick = ui.h('<input type="file" accept=".gts,.json" class="hidden">');
    pick.onchange = function () {
      var f = pick.files[0];
      if (!f) return;
      f.arrayBuffer().then(function (buf) {
        var s = L.skinFromFile(buf, f.name);
        if (!s) { ui.toast(ui.t('failed', f.name), true); return; }
        choose(s.name, s.tokens);
      });
    };
    view.appendChild(pick);
    var up = ui.h('<button class="btn" style="width:100%;margin-top:8px">' + ui.esc(ui.t('skin_upload')) + '</button>');
    up.onclick = function () { pick.click(); };
    view.appendChild(up);
  }

  function davPage(view) {
    back(view, ui.t('webdav'));
    ui.api('/api/dav/status').then(function (s) {
      var on = s.enabled;
      var swRow = ui.h('<div class="row set"><div><span class="t">' + ui.esc(ui.t('dav_on')) + '</span></div><button class="sw' +
        (on ? ' on' : '') + '"></button></div>');
      var swb = swRow.querySelector('.sw');
      swb.onclick = function () { on = !on; swb.className = 'sw' + (on ? ' on' : ''); };
      view.appendChild(swRow);
      var host = field(ui.t('dav_host'), 'text', s.host, 'nas.local'), port = field(ui.t('dav_port'), 'number', s.port);
      var user = field(ui.t('dav_user'), 'text', s.user), pass = field(ui.t('dav_pass'), 'password', '', s.pass_set ? ui.t('pass_kept') : '');
      var dir = field(ui.t('dav_path'), 'text', s.path, '/');
      var https = s.https;
      var hRow = ui.h('<div class="row set"><div><span class="t">HTTPS</span><small>' + ui.esc(ui.t('dav_https_s')) +
        '</small></div><button class="sw' + (https ? ' on' : '') + '"></button></div>');
      var hsw = hRow.querySelector('.sw');
      hsw.onclick = function () { https = !https; hsw.className = 'sw' + (https ? ' on' : ''); port.input.value = https ? 443 : 80; };
      view.appendChild(hRow);
      [host, port, user, pass, dir].forEach(function (f) { view.appendChild(f.el); });
      var save = ui.h('<button class="btn primary" style="width:100%">' + ui.esc(ui.t('save')) + '</button>');
      save.onclick = function () {
        var q = { on: on ? 1 : 0, https: https ? 1 : 0, host: host.input.value, port: port.input.value, user: user.input.value, path: dir.input.value };
        if (pass.input.value) q.pass = pass.input.value;
        ui.post('/api/dav/config?' + ui.q(q)).then(function (r) {
          if (!on) ui.toast(ui.t('saved'));
          else if (r.connected) ui.toast(ui.t('dav_ok'));
          else ui.toast(ui.t('dav_fail', r.error), true);
        }).catch(ui.fail);
      };
      view.appendChild(save);
    }).catch(ui.fail);
  }

  function wifiPage(view) {
    back(view, ui.t('home_wifi'));
    var sel = ui.h('<select class="input" style="margin-bottom:8px"><option value="">…</option></select>');
    var pw = field(ui.t('password'), 'password', '');
    var scan = ui.h('<button class="btn" style="width:100%;margin-bottom:8px">' + ui.esc(ui.t('scan')) + '</button>');
    scan.onclick = function () {
      ui.api('/scan').then(function (j) {
        var seen = {};
        sel.innerHTML = '';
        (j.networks || []).forEach(function (n) {
          if (!n.ssid || seen[n.ssid]) return;
          seen[n.ssid] = 1;
          var o = document.createElement('option');
          o.value = o.textContent = n.ssid;
          sel.appendChild(o);
        });
      }).catch(ui.fail);
    };
    var join = ui.h('<button class="btn primary" style="width:100%">' + ui.esc(ui.t('join')) + '</button>');
    join.onclick = function () {
      if ((ui.status || {}).loaded && ui.features.indexOf('classic') >= 0) {   // a dongle restarts to join: its RAM disk goes
        ui.confirm(ui.t('wifi_lose_q'), ui.t('wifi_lose_s'), ui.t('join'), save);
        return;
      }
      save();
    };
    function save() {
      var b = new URLSearchParams();
      b.append('ssid', sel.value);
      b.append('pass', pw.input.value);
      ui.api('/savewifi', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: b.toString() })
        .then(function () { ui.toast(ui.t('saved')); }).catch(ui.fail);
    }
    view.appendChild(ui.h('<div class="dim" style="margin-bottom:8px">' + ui.esc(ui.t('network')) + '</div>'));
    view.appendChild(sel);
    view.appendChild(scan);
    view.appendChild(pw.el);
    view.appendChild(join);
    scan.onclick();
  }

  function systemPage(view) {
    back(view, ui.t('system'));
    var s = ui.status || {}, i = ui.info || {};
    view.appendChild(row(ui.t('firmware'), (ui.drive && ui.drive.fw) || i.firmware));
    view.appendChild(row(ui.t('ip'), s.ip));
    view.appendChild(row(ui.t('heap'), Math.round((i.heap_free || 0) / 1024) + ' KB'));
    if (s.usb) view.appendChild(row(ui.t('stick'), s.usb.state === 'ready' ? s.usb.name + ' · ' + s.usb.size_mb + ' MB' : s.usb.state));
    var d = ui.drive || {};
    if (ui.features.indexOf('selftest') >= 0) view.appendChild(row(ui.t('head_pos'), 'cyl ' + (d.cylinder == null ? '-' : d.cylinder) + ' · head ' + (d.head == null ? '-' : d.head) +
      (d.motor ? ' · motor' : '')));
    if (ui.features.indexOf('selftest') >= 0) view.appendChild(row(ui.t('selftest'), ui.t('selftest_s'), function () {
      ui.toast(ui.t('loading'));
      ui.post('/api/drive/selftest?c=40&h=1&switch=0').then(function (r) {
        ui.toast(ui.t('selftest') + ': ' + (r.ok === false ? ui.t('failed', r.err || '') : 'OK'), r.ok === false);
      }).catch(ui.fail);
    }));
    var pick = ui.h('<input type="file" accept=".bin" class="hidden">');
    pick.onchange = function () {
      var f = pick.files[0];
      if (!f) return;
      var fd = new FormData();
      fd.append('firmware', f, f.name);
      ui.toast(ui.t('updating'));
      fetch('/api/system/ota', { method: 'POST', body: fd }).then(function (r) { return r.json(); }).then(function (j) {
        if (j.ok) ui.toast(ui.t('done_restart'));
        else ui.toast(ui.t('failed', j.error || j.err || ''), true);
      }).catch(ui.fail);
    };
    view.appendChild(pick);
    view.appendChild(row(ui.t('fw_update'), ui.t('fw_update_s'), function () { pick.click(); }));
    var rs = ui.h('<button class="btn danger" style="width:100%;margin-top:32px">' + ui.esc(ui.t('restart')) + '</button>');
    rs.onclick = function () {
      ui.confirm(ui.t('restart_q'), ui.t('restart_s'), ui.t('restart'), function () { ui.post('/api/system/reboot').catch(function () {}); });
    };
    view.appendChild(rs);
  }

  function fleetPage(view) {
    back(view, ui.t('fleet'));
    ui.api('/api/fleet').then(function (f) {
      var others = (f.devices || []).filter(function (d) { return !d.self; });
      if (!others.length) view.appendChild(ui.h('<div class="empty">' + ui.esc(ui.t('no_fleet')) + '</div>'));
      others.forEach(function (d) {
        var r = row(d.name || d.id, d.ip, function () { window.open('http://' + d.ip + '/', '_blank'); });
        view.appendChild(r);
      });
    }).catch(ui.fail);
  }

  var PAGES = { skin: skinPage, webdav: davPage, wifi: wifiPage, system: systemPage, fleet: fleetPage };

  ui.views.more = function (view, sub) {
    if (sub && PAGES[sub]) { PAGES[sub](view); return; }
    view.appendChild(ui.h('<div class="lbl">' + ui.esc(ui.t('settings')) + '</div>'));
    var lang = ui.h('<div class="row set"><div><span class="t">' + ui.esc(ui.t('language')) + '</span></div>' +
      '<span class="btns" style="flex:none"><button class="btn">EN</button><button class="btn">NL</button></span></div>');
    lang.querySelectorAll('button').forEach(function (b) {
      if (b.textContent.toLowerCase() === ui.lang) b.className = 'btn out';
      b.onclick = function () {
        var l = b.textContent.toLowerCase();
        ui.post('/api/lang?l=' + l).then(function () { ui.lang = l; ui.go('more'); }).catch(ui.fail);
      };
    });
    if (ui.features.indexOf('lang') >= 0) view.appendChild(lang);
    if (ui.features.indexOf('skins') >= 0) view.appendChild(row(ui.t('skin'), '', function () { ui.go('more', 'skin'); }));
    if (ui.features.indexOf('webdav') >= 0) view.appendChild(row(ui.t('webdav'), '', function () { ui.go('more', 'webdav'); }));
    if (ui.features.indexOf('wifi') >= 0) view.appendChild(row(ui.t('home_wifi'), (ui.status || {}).ssid, function () { ui.go('more', 'wifi'); }));
    view.appendChild(ui.h('<div class="lbl">' + ui.esc(ui.t('system')) + '</div>'));
    view.appendChild(row(ui.t('system'), (ui.drive && ui.drive.fw) || (ui.info || {}).firmware || '', function () { ui.go('more', 'system'); }));
    if (ui.features.indexOf('fleet') >= 0) view.appendChild(row(ui.t('fleet'), '', function () { ui.go('more', 'fleet'); }));
    // a Webby dongle keeps its own setup page (its access point, ESP-NOW or Wi-Fi) at /classic
    if (ui.features.indexOf('classic') >= 0) view.appendChild(row(ui.t('classic'), '', function () { location.href = '/classic'; }));
  };
})(UI);
