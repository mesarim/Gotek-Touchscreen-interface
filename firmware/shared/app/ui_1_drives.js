// Drives (UI unity Task 10): one card per drive (its disk, one status line, one action: eject, asked first), then per
// drive its settings: write protect, an empty disk, a disk from this computer; and DF1's switch.
(function (ui) {
  'use strict';
  ui.driveCards = function () { return AppLogic.driveCards(ui.status, ui.drive, ui.lang); };

  // Each drive's game (Dimitri 5 Nov): where its disk came from (/api/drive/source, asked when a disk changes), then its
  // cover and where a tap goes: the stick game's page, or its folder on the NAS.
  var art = [{}, {}];
  var srcKey = '';
  function findArt() {
    var st = ui.status || {};
    var key = (st.load_id || 0) + ':' + ((st.df1 && st.df1.load_id) || 0);
    if (key === srcKey) return;
    srcKey = key;
    var games = ui.games || ui.features.indexOf('games') < 0 ? Promise.resolve(ui.games) :
      ui.api('/api/games/list').then(function (l) { ui.games = l; return l; }).catch(function () { return null; });
    Promise.all([ui.api('/api/drive/source'), games]).then(function (r) {
      ['df0', 'df1'].forEach(function (k, d) {
        var s = r[0][k] || {};
        art[d] = {};
        var g = AppLogic.gameForDisk(r[1], s.stick);
        if (g) {
          art[d] = { url: g.cover ? AppLogic.fileUrl(g.cover) : '', go: function () { ui.go('games', g); } };
        } else if (s.nas && ui.davList) {
          var dir = AppLogic.nasFolder(s.nas);
          art[d] = { url: '', go: function () { ui.davOpen(dir); } };
          ui.davList(dir).then(function (l) {
            var plan = AppLogic.diskPlan(l.files);
            if (plan.cover) art[d].url = ui.davFile((dir === '/' ? '' : dir) + '/' + plan.cover);
            if (ui.tab === 'drives') ui.render();
          }).catch(function () {});
        }
      });
      if (ui.tab === 'drives') ui.render();
    }).catch(function () { srcKey = ''; });
  }

  function sw(on) { return '<button class="sw' + (on ? ' on' : '') + '" aria-pressed="' + (on ? 'true' : 'false') + '"></button>'; }
  function setRow(label, sub, on, fn) {
    var r = ui.h('<div class="row set"><div><span class="t">' + ui.esc(label) + '</span><small>' + ui.esc(sub) +
      '</small></div>' + sw(on) + '</div>');
    r.querySelector('.sw').onclick = fn;
    return r;
  }
  function actRow(label, sub, fn) {
    var r = ui.h('<div class="row"><div><span class="t">' + ui.esc(label) + '</span><small>' + ui.esc(sub) + '</small></div><span class="v">›</span></div>');
    r.onclick = fn;
    return r;
  }
  function n(id) { return id === 'DF1' ? 1 : 0; }

  function upload(drive, file) { ui.beforeLoad(drive, function () { send(drive, file); }); }
  function send(drive, file) {
    var fd = new FormData();
    fd.append('file', file, file.name);
    ui.toast(ui.t('loading'));
    fetch('/upload?drive=' + drive, { method: 'POST', body: fd }).then(function (r) {
      return r.json().then(function (j) { if (!r.ok) throw new Error(j.err || j.error || r.status); });
    }).then(function () { ui.toast(ui.t('loaded', 'DF' + drive)); ui.refresh(); }).catch(ui.fail);
  }

  ui.views.drives = function (view) {
    var st = ui.status || {};
    var stick = ui.features.indexOf('storage:usb') >= 0;
    findArt();
    ui.driveCards().forEach(function (c) {
      var d = n(c.id);
      var a = c.loaded ? art[d] : {};
      var el = ui.h('<div class="card drive' + (c.loaded ? ' in' : '') + '">' +
        (c.loaded ? '<div class="thumb">' + (a.url ? '<img alt="" src="' + ui.esc(a.url) + '">' : '') + '</div>' : '') +
        '<div><div class="cap">' + c.id + '</div>' +
        '<div class="name' + (a.go ? ' link' : '') + '">' + ui.esc(c.name) + (a.go ? ' ›' : '') + '</div><div class="dim">' +
        ui.esc(c.dimmed ? c.reason : c.line) + '</div></div></div>');
      if (a.go) {
        el.querySelector('.thumb').onclick = a.go;
        el.querySelector('.name').onclick = a.go;
      }
      if (c.loaded) {
        var b = ui.h('<button class="btn danger">' + ui.esc(ui.t('eject')) + '</button>');
        b.onclick = function () {
          ui.confirm(ui.t('eject_q', c.name), ui.t('eject_s', c.id), ui.t('eject'), function () {
            ui.post(d ? '/eject?drive=1' : '/eject').then(ui.refresh).catch(ui.fail);
          });
        };
        el.appendChild(b);
      }
      view.appendChild(el);
      if (ui.features.indexOf('wp') >= 0) view.appendChild(setRow(ui.t('wp') + ' ' + c.id, ui.t('wp_s'), c.wp, function () {
        ui.post('/api/disk/wp?' + ui.q({ drive: d, on: c.wp ? 0 : 1 })).then(ui.refresh).then(function () { ui.render(); }).catch(ui.fail);
      }));
      if (stick) view.appendChild(actRow(ui.t('blank'), ui.t('blank_s', c.id), function () {
        ui.beforeLoad(d, function () {
          ui.post('/api/usb/blank?drive=' + d).then(function () { ui.toast(ui.t('loaded', c.id)); ui.refresh(); }).catch(ui.fail);
        });
      }));
      var any = ui.features.indexOf('images:any') >= 0;   // a Webby dongle takes any image FlashFloppy reads (ADF, DSK, IMG, HFE...)
      var pick = ui.h('<input type="file"' + (any ? '' : ' accept=".adf,.ADF"') + ' class="hidden">');
      pick.onchange = function () { if (pick.files[0]) upload(d, pick.files[0]); };
      view.appendChild(pick);
      view.appendChild(actRow(ui.t('upload') + ' → ' + c.id, any ? ui.t('up_any', Math.round(((ui.status || {}).max || 0) / 1024)) : 'ADF, 880 KB',
        function () { pick.click(); }));
      view.appendChild(ui.h('<div class="lbl"></div>'));
    });
    if (ui.features.indexOf('df1') >= 0 || (st.df1 && 'on' in st.df1)) {
      var on = !!(st.df1 && st.df1.on);
      view.appendChild(setRow(ui.t('df1_on'), ui.t('df1_on_s'), on, function () {
        ui.post('/api/df1?on=' + (on ? 0 : 1)).then(function (r) { if (r && r.next) ui.toast(r.next); return ui.refresh(); })
          .then(function () { ui.render(); }).catch(ui.fail);
      }));
    }
  };
  ui.views.drives.live = true;
})(UI);
