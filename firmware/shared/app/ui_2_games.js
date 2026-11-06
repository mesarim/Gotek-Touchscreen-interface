// Games (UI unity Task 11): the games on the stick as cover tiles (covers load as they come into view), a search, and
// a game's page: cover, notes, its disks, where each one is, and "Load into" the drives this device has.
(function (ui) {
  'use strict';
  var L = AppLogic;
  var search = '';
  var favOnly = false;

  // A load from the stick runs on the device: /status usb.job shows it; the toast says how it ended.
  ui.loadFromStick = function (path, drive) { ui.beforeLoad(drive, function () { stickLoad(path, drive); }); };
  function stickLoad(path, drive) {
    ui.toast(ui.t('loading'));
    return ui.post('/api/usb/load?' + ui.q({ path: path, drive: drive })).then(function () {
      var tries = 0;
      (function wait() {
        ui.api('/status').then(function (s) {
          var j = s.usb && s.usb.job;
          if (j && j.state === 'done') { ui.toast(ui.t('loaded', ui.driveName(drive))); ui.refresh(); return; }
          if (j && j.state === 'error') { ui.toast(ui.t('failed', j.error), true); return; }
          if (++tries < 60) setTimeout(wait, 1000);
        }).catch(function () { if (++tries < 60) setTimeout(wait, 1000); });
      })();
    }).catch(ui.fail);
  }

  function initials(title) {
    return ui.esc(title.split(/\s+/).filter(Boolean).slice(0, 2).map(function (w) { return w[0]; }).join('').toUpperCase());
  }

  function tiles(view) {
    var input = ui.h('<input class="input search" type="search" placeholder="' + ui.esc(ui.t('search')) + '">');
    input.value = search;
    var lbl = ui.h('<div class="lbl"></div>');
    var chip = ui.h('<button class="chip' + (favOnly ? ' on' : '') + '">★ ' + ui.esc(ui.t('favs')) + '</button>');
    chip.onclick = function () { favOnly = !favOnly; chip.className = 'chip' + (favOnly ? ' on' : ''); fill(); };
    var grid = ui.h('<div class="grid"></div>');
    function fill() {
      var list = L.gameTiles(ui.games, search).filter(function (g) { return !favOnly || ui.isFav(L.favKey(g)); });
      lbl.textContent = ui.t('games_n', list.length);
      grid.innerHTML = '';
      if (!list.length && favOnly) grid.appendChild(ui.h('<div class="empty">' + ui.esc(ui.t('no_favs')) + '</div>'));
      list.forEach(function (g) {
        var p = L.program(g, ui.status, ui.features);
        var where = p.inDrive.filter(Boolean)[0];
        var n = g.disks.length;
        var el = ui.h('<div class="tile' + (where ? ' in' : '') + '" role="button" tabindex="0"><div class="pic">' + initials(g.title) +
          (g.cover ? '<img loading="lazy" alt="" src="' + ui.esc(p.coverUrl) + '">' : '') +
          (where ? '<em>' + where + '</em>' : '') + '</div><div class="tt"><div>' + ui.esc(g.title) +
          '<small>' + ui.esc(n === 1 ? ui.t('disk_1') : ui.t('disks_n', n)) + '</small></div></div></div>');
        var img = el.querySelector('img');
        if (img) img.onerror = function () { img.remove(); };
        el.onclick = function () { ui.go('games', g); };
        el.querySelector('.tt').appendChild(ui.star(L.favKey(g)));   // in the bar under the picture
        grid.appendChild(el);
      });
    }
    input.oninput = function () { search = input.value; fill(); };
    view.appendChild(input);
    view.appendChild(chip);
    view.appendChild(lbl);
    view.appendChild(grid);
    fill();
  }

  function page(view, g) {
    var p = L.program(g, ui.status, ui.features);
    var cur = Math.max(0, p.inDrive.indexOf('DF0'));
    var top = ui.h('<div class="bar2"><button class="btn">‹ ' + ui.esc(ui.t('tab_games')) + '</button><h2>' + ui.esc(g.title) + '</h2></div>');
    top.querySelector('button').onclick = function () { ui.go('games'); };
    top.appendChild(ui.star(L.favKey(g)));
    view.appendChild(top);
    var wrap = ui.h('<div class="prog"><div class="cover">' + (g.cover ? '<img alt="" src="' + ui.esc(p.coverUrl) + '">' : '') +
      '</div><div class="info"></div></div>');
    var info = wrap.querySelector('.info');
    var diskRow = ui.h('<div class="btns"></div>');
    var where = ui.h('<div class="where"></div>');
    var acts = ui.h('<div class="btns"></div>');
    function draw() {
      p = L.program(g, ui.status, ui.features);
      diskRow.innerHTML = '';
      if (p.disks.length > 1) {
        var prev = ui.h('<button class="btn">‹</button>'), next = ui.h('<button class="btn">›</button>');
        prev.disabled = cur === 0;
        next.disabled = cur === p.disks.length - 1;
        prev.onclick = function () { cur--; draw(); };
        next.onclick = function () { cur++; draw(); };
        diskRow.appendChild(prev);
        diskRow.appendChild(ui.h('<div class="btn" style="flex:3">' + ui.esc(ui.t('disk_n_of', cur + 1, p.disks.length)) + '</div>'));
        diskRow.appendChild(next);
      }
      where.textContent = (p.inDrive[cur] ? ui.t('disk_in', cur + 1, p.inDrive[cur]) : p.names[cur]) +
        (p.df0Only ? ' · ' + ui.t('df0_only') : '');
      acts.innerHTML = '';
      p.targets.forEach(function (id) {
        var here = p.inDrive[cur] === id;
        var b = ui.h('<button class="btn ' + (here ? 'out' : 'primary') + '">' + ui.esc(here ? ui.t('in_drive', id) : 'Load ' + id) + '</button>');
        b.disabled = here;
        b.onclick = function () { ui.loadFromStick(p.disks[cur], id === 'DF1' ? 1 : 0); };
        acts.appendChild(b);
      });
    }
    info.appendChild(diskRow);
    info.appendChild(where);
    info.appendChild(acts);
    if (g.nfo) {
      var nfo = ui.h('<div class="card"><pre class="nfo"></pre></div>');
      info.appendChild(nfo);
      fetch(p.nfoUrl).then(function (r) { return r.ok ? r.text() : ''; }).then(function (t) { nfo.querySelector('pre').textContent = t; });
    }
    if (g.manual) info.appendChild(ui.h('<a class="btn" target="_blank" href="' + ui.esc(p.manualUrl) + '">' + ui.esc(ui.t('manual')) + '</a>'));
    view.appendChild(wrap);
    draw();
    ui.views.games.redraw = draw;
  }

  ui.views.games = function (view, sub) {
    ui.views.games.redraw = null;
    if (sub) { page(view, sub); return; }
    if (ui.games) tiles(view);
    else view.appendChild(ui.h('<div class="empty">' + ui.esc(ui.t('loading')) + '</div>'));
    ui.api('/api/games/list').then(function (list) {
      var first = !ui.games;
      ui.games = list;
      if (ui.tab === 'games' && !ui.sub && first) ui.render();
    }).catch(function (e) {
      if (ui.tab !== 'games' || ui.sub) return;
      view.innerHTML = '<div class="empty">' + ui.esc(e.code === 'no_stick' ? ui.t('no_stick') : ui.t('failed', e.message)) + '</div>';
    });
  };
})(UI);
