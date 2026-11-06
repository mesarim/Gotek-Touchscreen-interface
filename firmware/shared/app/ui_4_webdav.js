// WebDAV (UI unity Task 12; fast, Dimitri 5 Nov): browse the NAS. A big folder gets a letter bar and a search; each game
// folder in view shows its disks (fetched one folder at a time, only for the rows in view) and DF0 / DF0+DF1 to load
// it straight away. Lists are kept in the page for the session (and in the FluxDrive for 5 minutes); Refresh asks the
// NAS again. A folder with a cover and notes shows them; for a disk: load it into a drive, or keep it on the stick.
(function (ui) {
  'use strict';
  var L = AppLogic;
  var path = '/';
  var letter = '';
  var search = '';
  var lists = {};      // path -> /api/dav/list's reply, for this session
  var gen = 0;         // a newer view stops the older one's lookups
  function isAdf(n) { return /\.adf$/i.test(n); }
  function join(dir, name) { return (dir === '/' ? '' : dir) + '/' + name; }
  function davFile(p) { return '/api/dav/file?path=' + encodeURIComponent(p); }

  function list(p, fresh) {
    if (!fresh && lists[p]) return Promise.resolve(lists[p]);
    return ui.api('/api/dav/list?' + ui.q(fresh ? { path: p, fresh: 1 } : { path: p })).then(function (l) { lists[p] = l; return l; });
  }

  // Disks of the NAS folder dir into drives (0, 1), one after the other, as one job: the questions first (a disk that
  // would lose the Amiga's writes), then the folder's row, its page and each disk's row fill until every disk is in.
  function loadDisks(dir, disks, drives) {
    (function ask(i) {
      if (i < drives.length) { ui.beforeLoad(drives[i], function () { ask(i + 1); }); return; }
      if (!ui.jobStart(dir, 'load', disks, drives.map(ui.driveName))) return;
      (function next(k) {
        if (k >= disks.length) { ui.jobSet({ state: 'ok' }); ui.refresh(); return; }
        ui.postJob('/api/dav/load?' + ui.q({ path: join(dir, disks[k]), drive: drives[k] }))
          .then(function () { ui.jobSet({ done: k + 1 }); ui.refresh(); next(k + 1); })
          .catch(function (e) { ui.jobSet({ state: 'fail', error: ui.errText(e) }); });
      })(0);
    })(0);
  }
  // Disks of a NAS folder onto the stick, one after the other (each with the folder's cover and notes), as one job; one
  // already there is passed over.
  function keepDisks(dir, disks) {
    if (!ui.jobStart(dir, 'keep', disks)) return;
    (function next(i) {
      if (i >= disks.length) { ui.jobSet({ state: 'ok' }); ui.games = null; return; }
      ui.postJob('/api/dav/keep?' + ui.q({ path: join(dir, disks[i]) })).then(function () { ui.jobSet({ done: i + 1 }); next(i + 1); })
        .catch(function (e) {
          if (e.code === 'exists') { ui.jobSet({ done: i + 1 }); next(i + 1); return; }
          ui.jobSet({ state: 'fail', error: ui.errText(e) });
        });
    })(0);
  }
  // A game folder: disk 1 into DF0, and with both: disk 2 into DF1.
  function loadGame(dir, plan, both) {
    if (both && plan.disks[1]) loadDisks(dir, plan.disks.slice(0, 2), [0, 1]);
    else loadDisks(dir, [plan.disks[0]], [0]);
  }

  function diskSheet(name) {
    var full = join(path, name);
    var b = [{ label: ui.t('cancel') }];
    ['DF0', 'DF1'].forEach(function (id) {
      if (id === 'DF1' && ui.features.indexOf('df1') < 0) return;
      b.push({ label: 'Load ' + id, cls: 'primary', fn: function () { loadDisks(path, [name], [id === 'DF1' ? 1 : 0]); } });
    });
    if (ui.features.indexOf('storage:usb') >= 0) b.push({ label: ui.t('keep'), keep: true, fn: function () {
      var all = (lists[path] ? L.diskPlan(lists[path].files).disks : [name]);
      if (all.length < 2) { ui.close(); keepDisks(path, [name]); return; }
      ui.sheet(ui.t('keep'), name, [{ label: ui.t('cancel') }, { label: ui.t('keep_one'), fn: function () { keepDisks(path, [name]); } },
        { label: ui.t('keep_game', all.length), cls: 'primary', fn: function () { keepDisks(path, all); } }]);
    } });
    ui.sheet(name, '880 KB', b);
  }

  // A game folder's row: its name opens it; its disks show when known; DF0 and (from two disks) DF0+DF1 load it.
  function gameRow(dir, name, lookups) {
    var r = ui.h('<div class="row"><div><span class="t">' + ui.esc(name) + '</span><small>…</small></div>' +
      '<span class="btns" style="flex:none"></span></div>');
    var small = r.querySelector('small'), btns = r.querySelector('.btns');
    r.insertBefore(ui.star(L.favKey(null, join(dir, name))), r.firstChild);
    ui.jobBind(r, join(dir, name), small);
    r.onclick = function () { path = join(dir, name); letter = ''; search = ''; ui.render(); };
    lookups.push(function () {
      return list(join(dir, name)).then(function (l) {
        var plan = L.diskPlan(l.files);
        if (!plan.disks.length) { small.textContent = l.dirs.length ? l.dirs.length + ' ›' : ''; ui.jobText(small); return; }
        var only = L.df0Only(name);
        small.textContent = (plan.disks.length === 1 ? ui.t('disk_1') : ui.t('disks_n', plan.disks.length)) +
          (only ? ' · ' + ui.t('df0_only') : '');
        ui.jobText(small);
        var b0 = ui.h('<button class="btn">DF0</button>');
        b0.onclick = function (e) { e.stopPropagation(); loadGame(join(dir, name), plan, false); };
        btns.appendChild(b0);
        if (plan.disks.length > 1 && ui.features.indexOf('df1') >= 0 && !only) {
          var b01 = ui.h('<button class="btn primary">DF0+DF1</button>');
          b01.onclick = function (e) { e.stopPropagation(); loadGame(join(dir, name), plan, true); };
          btns.appendChild(b01);
        }
      }).catch(function () { small.textContent = ''; });
    });
    return r;
  }

  // The rows in view look up their disks one folder at a time; a newer view stops this one.
  function lookUp(lookups, mine) {
    (function next(i) {
      if (i >= lookups.length || mine !== gen) return;
      lookups[i]().then(function () { next(i + 1); });
    })(0);
  }

  function folder(body, l, s) {
    var dirs = l.dirs, big = dirs.length > 30;
    var cr = ui.h('<div class="crumbs"></div>');
    L.crumbs(path).forEach(function (c, i) {
      if (i > 0) cr.appendChild(ui.h('<span class="dim">/</span>'));
      var b = ui.h('<button>' + ui.esc(c.name === '/' ? s.host : c.name) + '</button>');
      b.onclick = function () { path = c.path; letter = ''; search = ''; ui.render(); };
      cr.appendChild(b);
    });
    var again = ui.h('<button>↻ ' + ui.esc(ui.t('refresh')) + '</button>');
    again.onclick = function () { list(path, true).then(function () { ui.render(); }).catch(ui.fail); };
    cr.appendChild(again);
    body.appendChild(cr);
    var plan = L.diskPlan(l.files);
    if (plan.cover || plan.nfo) {
      var card = ui.h('<div class="prog" style="margin-bottom:16px">' + (plan.cover ? '<div class="cover"><img alt="" src="' +
        ui.esc(davFile(join(path, plan.cover))) + '"></div>' : '') + '<div class="info"><pre class="nfo"></pre></div></div>');
      body.appendChild(card);
      if (plan.nfo) fetch(davFile(join(path, plan.nfo))).then(function (r) { return r.ok ? r.text() : ''; })
        .then(function (t) { card.querySelector('pre').textContent = t; });
    }
    if (plan.disks.length) {
      var acts = ui.h('<div class="btns" style="margin-bottom:16px"></div>');
      var a0 = ui.h('<button class="btn primary">DF0</button>');
      a0.onclick = function () { loadGame(path, plan, false); };
      acts.appendChild(a0);
      if (plan.disks.length > 1 && ui.features.indexOf('df1') >= 0 && !L.df0Only(path.split('/').pop())) {
        var a01 = ui.h('<button class="btn primary">DF0+DF1</button>');
        a01.onclick = function () { loadGame(path, plan, true); };
        acts.appendChild(a01);
      }
      if (ui.features.indexOf('storage:usb') >= 0) {
        var ka = ui.h('<button class="btn">' + ui.esc(ui.t('keep_all')) + '</button>');
        ka.onclick = function () { keepDisks(path, plan.disks); };
        acts.appendChild(ka);
      }
      body.appendChild(acts);
      var jl = ui.h('<div class="jobline"><span class="t"></span></div>');   // the whole job, under the buttons
      body.appendChild(ui.jobBind(jl, path, jl.firstChild, null, true));
    }
    var rows = ui.h('<div></div>');
    if (big) {
      var input = ui.h('<input class="input search" type="search" placeholder="' + ui.esc(ui.t('search')) + '">');
      input.value = search;
      var bar = ui.h('<div class="crumbs letters"></div>');
      var ls = ['★'].concat(L.letters(dirs));
      if (!letter || ls.indexOf(letter) < 0) letter = ls[1] || '';
      ls.forEach(function (c) {
        var b = ui.h('<button' + (c === letter && !search ? ' class="on"' : '') + '>' + c + '</button>');
        b.onclick = function () { letter = c; search = ''; ui.render(); };
        bar.appendChild(b);
      });
      var t;
      input.oninput = function () { search = input.value; clearTimeout(t); t = setTimeout(fill, 200); };
      body.appendChild(input);
      body.appendChild(bar);
    }
    body.appendChild(rows);
    function fill() {
      var me = ++gen;
      rows.innerHTML = '';
      var lookups = [];
      var show = big ? (letter === '★' && !search ? dirs.filter(function (d) { return ui.isFav(L.favKey(null, join(path, d))); })
        : L.pickRows(dirs, letter, search)) : dirs;
      if (!show.length && !l.files.length) rows.appendChild(ui.h('<div class="empty">' + ui.esc(ui.t(letter === '★' ? 'no_favs' : 'empty_folder')) + '</div>'));
      show.forEach(function (d) { rows.appendChild(gameRow(path, d, lookups)); });
      l.files.filter(function (f) { return isAdf(f.name); }).forEach(function (f) {
        var r = ui.h('<div class="row"><div><span class="t">' + ui.esc(f.name) + '</span><small>' +
          Math.round(f.size / 1024) + ' KB</small></div><span class="v">⋯</span></div>');
        r.onclick = function () { diskSheet(f.name); };
        ui.jobBind(r, path, r.querySelector('small'), f.name);
        rows.appendChild(r);
      });
      lookUp(lookups, me);
    }
    fill();
  }

  // For the drive cards: a NAS folder's list (kept), a file's URL, and opening a folder here.
  ui.davList = list;
  ui.davFile = davFile;
  ui.davOpen = function (dir) { path = dir || '/'; letter = ''; search = ''; ui.go('webdav'); };

  ui.views.webdav = function (view) {
    gen++;   // a lookup of the view before stops
    var body = ui.h('<div><div class="empty">' + ui.esc(ui.t('loading')) + '</div></div>');
    view.appendChild(body);
    ui.api('/api/dav/status').then(function (s) {
      if (!s.enabled || !s.host) {
        body.innerHTML = '';
        var e = ui.h('<div class="empty">' + ui.esc(ui.t('dav_off')) + '<br></div>');
        var go = ui.h('<button class="btn primary">' + ui.esc(ui.t('dav_setup')) + '</button>');
        go.onclick = function () { ui.go('more', 'webdav'); };
        e.appendChild(go);
        body.appendChild(e);
        return;
      }
      return list(path).then(function (l) {
        body.innerHTML = '';
        folder(body, l, s);
      });
    }).catch(function (e) {
      body.innerHTML = '<div class="empty">' + ui.esc(e.code === 'motor' ? ui.t('busy_try') : ui.t('dav_fail', e.message)) + '</div>';
    });
  };
})(UI);
