// Files (UI unity Task 11): the stick's folders and disks: open a folder, load a disk, rename, move, a new folder, an
// upload, and delete (asked first). The stick's listing (/api/usb/list) shows folders and ADFs.
(function (ui) {
  'use strict';
  var L = AppLogic;
  var path = '/usb';

  function ask(title, label, value, fn) {
    var input = ui.h('<input class="input">');
    input.value = value || '';
    ui.sheet(title, '', [{ label: ui.t('cancel') }, { label: label, cls: 'primary', fn: function () { fn(input.value.trim()); } }], input);
    setTimeout(function () { input.focus(); }, 50);
  }
  function done(msg) { return function () { if (msg) ui.toast(msg); ui.render(); }; }

  function fileSheet(name) {
    var full = path + '/' + name;
    var b = [{ label: ui.t('cancel') }];
    ['DF0', 'DF1'].forEach(function (id) {
      if (id === 'DF1' && ui.features.indexOf('df1') < 0) return;
      b.push({ label: 'Load ' + id, cls: 'primary', fn: function () { ui.loadFromStick(full, id === 'DF1' ? 1 : 0); } });
    });
    var more = ui.h('<div class="btns"></div>');
    var favRow = ui.h('<div class="row set"><div><span class="t">' + ui.esc(ui.t('fav')) + '</span></div></div>');
    favRow.appendChild(ui.star(full));
    more.appendChild(favRow);
    [['contents', function () { contents(full, name); }], ['rename', function () {
      ask(ui.t('rename'), ui.t('save'), name, function (n) {
        if (!n) return;
        if (!/\.adf$/i.test(n)) n += '.adf';
        ui.post('/api/usb/move?' + ui.q({ from: full, to: path + '/' + n })).then(done(ui.t('saved'))).catch(ui.fail);
      });
    }], ['move', function () {
      ask(ui.t('move_to'), ui.t('move'), path, function (dir) {
        if (!dir) return;
        ui.post('/api/usb/move?' + ui.q({ from: full, to: dir.replace(/\/+$/, '') + '/' + name })).then(done(ui.t('saved'))).catch(ui.fail);
      });
    }], ['delete', function () {
      ui.confirm(ui.t('delete_q', name), ui.t('delete_s'), ui.t('delete'), function () {
        ui.post('/api/usb/delete?' + ui.q({ path: full })).then(done()).catch(ui.fail);
      });
    }]].forEach(function (a) {
      var el = ui.h('<button class="btn' + (a[0] === 'delete' ? ' danger' : '') + '">' + ui.esc(ui.t(a[0])) + '</button>');
      el.onclick = function () { ui.close(); a[1](); };
      more.appendChild(el);
    });
    ui.sheet(name, '', b, more);
  }

  // What is on a disk (AmigaDOS): read whole from the stick, then its folders and files (AppLogic.adfDir).
  function tree(entries) {
    var ul = ui.h('<ul class="tree"></ul>');
    entries.forEach(function (e) {
      var li = ui.h('<li>' + (e.dir ? '▸ ' : '') + ui.esc(e.name) + (e.dir ? '' : '<small>' + e.size + ' B</small>') + '</li>');
      if (e.dir && e.entries.length) li.appendChild(tree(e.entries));
      ul.appendChild(li);
    });
    return ul;
  }
  function contents(full, name) {
    ui.toast(ui.t('reading'));
    fetch('/api/usb/file?' + ui.q({ path: full })).then(function (r) {
      if (!r.ok) throw new Error('HTTP ' + r.status);
      return r.arrayBuffer();
    }).then(function (buf) {
      var d = L.adfDir(buf);
      var box = ui.h('<div class="scroll"></div>');
      box.appendChild(d.entries.length ? tree(d.entries) : ui.h('<div class="empty">' + ui.esc(ui.t('empty_folder')) + '</div>'));
      ui.sheet(d.name || name, d.kind, [{ label: ui.t('back') }], box);
    }).catch(ui.fail);
  }

  function upload(file) {
    var fd = new FormData();
    fd.append('file', file, file.name);
    ui.toast(ui.t('loading'));
    fetch('/api/usb/upload?' + ui.q({ path: path }), { method: 'POST', body: fd }).then(function (r) {
      return r.json().then(function (j) { if (!r.ok) throw new Error(j.error || r.status); });
    }).then(done(ui.t('uploaded'))).catch(ui.fail);
  }

  ui.views.files = function (view) {
    var cr = ui.h('<div class="crumbs"></div>');
    L.crumbs(path).forEach(function (c, i) {
      if (i) cr.appendChild(ui.h('<span class="dim">/</span>'));
      var b = ui.h('<button>' + ui.esc(c.name) + '</button>');
      b.onclick = function () { path = c.path; ui.render(); };
      cr.appendChild(b);
    });
    view.appendChild(cr);
    var tools = ui.h('<div class="btns" style="margin-bottom:16px"></div>');
    var pick = ui.h('<input type="file" class="hidden">');
    pick.onchange = function () { if (pick.files[0]) upload(pick.files[0]); };
    var up = ui.h('<button class="btn">' + ui.esc(ui.t('upload')) + '</button>');
    up.onclick = function () { pick.click(); };
    var mk = ui.h('<button class="btn">' + ui.esc(ui.t('new_folder')) + '</button>');
    mk.onclick = function () {
      ask(ui.t('folder_name'), ui.t('save'), '', function (n) {
        if (n) ui.post('/api/usb/mkdir?' + ui.q({ path: path + '/' + n })).then(done()).catch(ui.fail);
      });
    };
    tools.appendChild(up);
    tools.appendChild(mk);
    tools.appendChild(pick);
    view.appendChild(tools);
    var list = ui.h('<div><div class="empty">' + ui.esc(ui.t('loading')) + '</div></div>');
    view.appendChild(list);
    ui.api('/api/usb/list?' + ui.q({ path: path })).then(function (l) {
      list.innerHTML = '';
      if (!l.dirs.length && !l.adfs.length) {
        var e = ui.h('<div class="empty">' + ui.esc(ui.t('empty_folder')) + '</div>');
        if (path !== '/usb') {
          var del = ui.h('<button class="btn danger">' + ui.esc(ui.t('delete')) + '</button>');
          del.onclick = function () {
            ui.confirm(ui.t('delete_q', path.split('/').pop()), ui.t('delete_s'), ui.t('delete'), function () {
              ui.post('/api/usb/delete?' + ui.q({ path: path })).then(function () { path = path.replace(/\/[^/]*$/, ''); ui.render(); }).catch(ui.fail);
            });
          };
          e.appendChild(ui.h('<br>'));
          e.appendChild(del);
        }
        list.appendChild(e);
      }
      l.dirs.forEach(function (d) {
        var r = ui.h('<div class="row"><div><span class="t">' + ui.esc(d) + '</span></div><span class="v">›</span></div>');
        r.onclick = function () { path = path + '/' + d; ui.render(); };
        list.appendChild(r);
      });
      l.adfs.forEach(function (f) {
        var r = ui.h('<div class="row"><div><span class="t">' + (ui.isFav(path + '/' + f.name) ? '★ ' : '') + ui.esc(f.name) +
          '</span><small>880 KB</small></div><span class="v">⋯</span></div>');
        r.onclick = function () { fileSheet(f.name); };
        list.appendChild(r);
      });
    }).catch(function (e) {
      list.innerHTML = '<div class="empty">' + ui.esc(e.code === 'no_stick' ? ui.t('no_stick') : ui.t('failed', e.message)) + '</div>';
    });
  };
})(UI);
