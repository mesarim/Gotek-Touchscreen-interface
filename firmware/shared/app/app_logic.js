// The app's pure logic (docs/house-style.md, docs/superpowers/specs/2026-11-05-ui-unity-design.md): no DOM, no fetch.
// Inlined into the page by tools/make_app.py; node --test firmware/test/web loads it as a module.
var AppLogic = (function () {
  'use strict';

  var TOKENS = ['bg', 'bar', 'card', 'line', 'txt', 'dim', 'accent', 'ok', 'warn', 'ins', 'danger'];
  var NEO = { bg: '#081021', bar: '#080C19', card: '#101831', line: '#42598C', txt: '#EFF3FF', dim: '#94A2BD',
              accent: '#F7B231', ok: '#52D27B', warn: '#F78E31', ins: '#7BD2F7', danger: '#FF6B6B' };
  // The old GTi colour themes (webui.html THEMES), as skins without an atlas.
  var THEMES = {
    AMIGA_WB13: { fill: '#0055aa', light: '#ffffff', dark: '#000044', text: '#ffffff', accent: '#ff8800' },
    AMIGA_WB2: { fill: '#a8a8a8', light: '#ffffff', dark: '#000000', text: '#000000', accent: '#5a6ea0' },
    PAPER_WHITE: { fill: '#eeeeea', light: '#ffffff', dark: '#787874', text: '#141414', accent: '#285abe' },
    MIDNIGHT: { fill: '#222630', light: '#485062', dark: '#0c0e14', text: '#dee4f0', accent: '#5aaaff' },
    PHOSPHOR: { fill: '#08180c', light: '#28c85a', dark: '#040c06', text: '#50ff82', accent: '#50ff82' },
    OMEGA_DARK: { fill: '#181c31', light: '#3b4570', dark: '#0b0e22', text: '#e9ecf5', accent: '#3fe0e8' }
  };

  var STRINGS = {
    en: {
      tab_drives: 'Drives', tab_games: 'Games', tab_files: 'Files', tab_webdav: 'WebDAV', tab_more: 'More',
      no_disk: 'No disk', eject: 'Eject', eject_q: 'Eject {0}?', eject_s: 'The disk leaves {0}. What the Amiga wrote stays.',
      cancel: 'Cancel', load_into: 'Load into…', in_drive: 'in {0}', disk_n_of: 'Disk {0} of {1}',
      disk_in: 'Disk {0}: in {1}', blank: 'Empty disk', blank_s: 'A new formatted disk in {0}', wp: 'Write protect',
      wp_s: 'The Amiga cannot write to this disk', amiga_busy: 'Amiga busy', amiga_on: 'Amiga on', amiga_off: 'Amiga off',
      stored: 'Stored', kept_stick: 'kept on the stick', not_kept: 'not kept: no stick', keeping: 'being kept on the stick',
      off: 'off', df1_on: 'DF1 on', df1_on_s: 'A second drive for the Amiga (reset the Amiga after a change)',
      no_stick: 'No stick in the FluxDrive.', games_n: '{0} games on the stick', search: 'Search',
      notes: 'Notes', manual: 'Manual', back: 'Back', disks_n: '{0} disks', disk_1: '1 disk',
      upload: 'Upload', new_folder: 'New folder', rename: 'Rename', move: 'Move', delete: 'Delete',
      delete_q: 'Delete {0}?', delete_s: 'It is gone from the stick for good.', folder_name: 'Folder name',
      new_name: 'New name', move_to: 'Move to folder', empty_folder: 'Nothing here.', uploaded: 'Uploaded',
      dav_off: 'WebDAV is not set up.', dav_setup: 'Set up', keep: 'Keep on stick', kept: 'Kept on the stick',
      loading: 'Loading…', loaded: 'Loaded into {0}', failed: 'Failed: {0}', busy_try: 'Amiga busy, try again',
      settings: 'Settings', system: 'System', fleet: 'Fleet', wifi: 'Wi-Fi', webdav: 'WebDAV', language: 'Language',
      skin: 'Skin', skin_s: 'Colours of this FluxDrive', skin_upload: 'Upload a skin (.gts)', save: 'Save',
      saved: 'Saved', dav_on: 'WebDAV on', dav_host: 'NAS address', dav_port: 'Port', dav_user: 'User',
      dav_pass: 'Password', dav_path: 'Folder on the NAS', dav_ok: 'Connected to the NAS', dav_fail: 'NAS not reached: {0}',
      pass_kept: 'leave empty to keep it', firmware: 'Firmware', fw_update: 'Update firmware', fw_update_s: 'A fluxdrive.bin',
      restart: 'Restart', restart_q: 'Restart the FluxDrive?', restart_s: 'The drives are gone for a few seconds.',
      ip: 'IP address', heap: 'Free memory', stick: 'Stick', home_wifi: 'Home Wi-Fi', network: 'Network',
      password: 'Password', scan: 'Scan', join: 'Save & join', no_fleet: 'No other devices seen.', updating: 'Updating…',
      done_restart: 'Done, the FluxDrive restarts.', storing: 'Storing…',
      lose_q: 'Replace the disk in {0}?', lose_s: 'What the Amiga wrote to it is not on the stick and will be lost.', replace: 'Replace',
      keep_failed: 'not kept: the stick refused it', fav: 'Favourite', favs: 'Favourites', no_favs: 'No favourites yet.', contents: 'Contents', reading: 'Reading the disk…', refresh: 'Refresh', df0_only: 'DF0 only', keep_one: 'Only this disk', keep_game: 'Whole game ({0} disks)',
      keep_all: 'Whole game on stick', keeping_n: 'Keeping {0} of {1}…', kept_n: '{0} disks on the stick', dav_https_s: 'Encrypted (the certificate is not checked)', head_pos: 'Head', selftest: 'Self-test', selftest_s: 'The drive checks itself on cylinder 40, head 1',
      up_any: 'ADF, DSK, IMG, HFE… up to {0} KB', job_load: 'Disk {0} of {1} → {2}…', job_keep: 'Disk {0} of {1} to the stick…', job_in: 'disk {0} in {1}',
      job_kept_all: 'All {0} disks on the stick', job_kept_1: 'On the stick', job_fail: 'Disk {0}: {1}', job_busy: 'Wait: another transfer is still running', classic: 'Setup page (access point, ESP-NOW)'
    },
    nl: {
      tab_drives: 'Drives', tab_games: 'Games', tab_files: 'Bestanden', tab_webdav: 'WebDAV', tab_more: 'Meer',
      no_disk: 'Geen disk', eject: 'Uitwerpen', eject_q: '{0} uitwerpen?', eject_s: 'De disk gaat uit {0}. Wat de Amiga schreef blijft.',
      cancel: 'Annuleren', load_into: 'Laden in…', in_drive: 'in {0}', disk_n_of: 'Disk {0} van {1}',
      disk_in: 'Disk {0}: in {1}', blank: 'Lege disk', blank_s: 'Een nieuwe geformatteerde disk in {0}', wp: 'Schrijfbeveiliging',
      wp_s: 'De Amiga kan niet op deze disk schrijven', amiga_busy: 'Amiga bezig', amiga_on: 'Amiga aan', amiga_off: 'Amiga uit',
      stored: 'Opgeslagen', kept_stick: 'bewaard op de stick', not_kept: 'niet bewaard: geen stick', keeping: 'wordt op de stick bewaard',
      off: 'uit', df1_on: 'DF1 aan', df1_on_s: 'Een tweede drive voor de Amiga (reset de Amiga na een wijziging)',
      no_stick: 'Geen stick in de FluxDrive.', games_n: '{0} games op de stick', search: 'Zoeken',
      notes: 'Info', manual: 'Handleiding', back: 'Terug', disks_n: '{0} disks', disk_1: '1 disk',
      upload: 'Uploaden', new_folder: 'Nieuwe map', rename: 'Hernoemen', move: 'Verplaatsen', delete: 'Verwijderen',
      delete_q: '{0} verwijderen?', delete_s: 'Het is dan echt weg van de stick.', folder_name: 'Naam van de map',
      new_name: 'Nieuwe naam', move_to: 'Naar map', empty_folder: 'Hier staat niets.', uploaded: 'Geüpload',
      dav_off: 'WebDAV is nog niet ingesteld.', dav_setup: 'Instellen', keep: 'Op stick bewaren', kept: 'Bewaard op de stick',
      loading: 'Laden…', loaded: 'Geladen in {0}', failed: 'Mislukt: {0}', busy_try: 'Amiga bezig, probeer het zo nog eens',
      settings: 'Instellingen', system: 'Systeem', fleet: 'Vloot', wifi: 'Wi-Fi', webdav: 'WebDAV', language: 'Taal',
      skin: 'Skin', skin_s: 'Kleuren van deze FluxDrive', skin_upload: 'Skin uploaden (.gts)', save: 'Opslaan',
      saved: 'Opgeslagen', dav_on: 'WebDAV aan', dav_host: 'Adres van de NAS', dav_port: 'Poort', dav_user: 'Gebruiker',
      dav_pass: 'Wachtwoord', dav_path: 'Map op de NAS', dav_ok: 'Verbonden met de NAS', dav_fail: 'NAS niet bereikt: {0}',
      pass_kept: 'leeg laten om het te houden', firmware: 'Firmware', fw_update: 'Firmware bijwerken', fw_update_s: 'Een fluxdrive.bin',
      restart: 'Herstarten', restart_q: 'FluxDrive herstarten?', restart_s: 'De drives zijn een paar seconden weg.',
      ip: 'IP-adres', heap: 'Vrij geheugen', stick: 'Stick', home_wifi: 'Thuis-Wi-Fi', network: 'Netwerk',
      password: 'Wachtwoord', scan: 'Zoeken', join: 'Opslaan & verbinden', no_fleet: 'Geen andere apparaten gezien.', updating: 'Bijwerken…',
      done_restart: 'Klaar, de FluxDrive herstart.', storing: 'Opslaan…',
      lose_q: 'Disk in {0} vervangen?', lose_s: 'Wat de Amiga erop schreef staat niet op de stick en gaat verloren.', replace: 'Vervangen',
      keep_failed: 'niet bewaard: de stick weigerde', fav: 'Favoriet', favs: 'Favorieten', no_favs: 'Nog geen favorieten.', contents: 'Inhoud', reading: 'Disk lezen…', refresh: 'Vernieuwen', df0_only: 'alleen DF0', keep_one: 'Alleen deze disk', keep_game: 'Hele spel ({0} disks)',
      keep_all: 'Hele spel op stick', keeping_n: 'Bewaren {0} van {1}…', kept_n: '{0} disks op de stick', dav_https_s: 'Versleuteld (het certificaat wordt niet gecontroleerd)', head_pos: 'Kop', selftest: 'Zelftest', selftest_s: 'De drive test zichzelf op cilinder 40, kop 1',
      up_any: 'ADF, DSK, IMG, HFE… tot {0} KB', job_load: 'Disk {0} van {1} → {2}…', job_keep: 'Disk {0} van {1} naar de stick…', job_in: 'disk {0} in {1}',
      job_kept_all: 'Alle {0} disks staan op de stick', job_kept_1: 'Op de stick', job_fail: 'Disk {0}: {1}', job_busy: 'Wacht: er loopt nog een overdracht', classic: 'Instelpagina (access point, ESP-NOW)'
    }
  };

  function t(key, lang, args) {
    var s = (STRINGS[lang] && STRINGS[lang][key]) || STRINGS.en[key] || key;
    if (args) for (var i = 0; i < args.length; i++) s = s.split('{' + i + '}').join(String(args[i]));
    return s;
  }

  var TAB_FEATURE = { drives: 'drives', games: 'games', files: 'files', webdav: 'webdav' };
  function tabsFor(features) {
    var out = [];
    ['drives', 'games', 'files', 'webdav'].forEach(function (tab) {
      if (features.indexOf(TAB_FEATURE[tab]) >= 0) out.push(tab);
    });
    out.push('more');
    return out;
  }

  // The features with the stick's (storage:usb, games, files) on or off as /status says the stick is ready.
  var STICK = ['storage:usb', 'games', 'files'];
  function withStick(features, ready) {
    var rest = features.filter(function (f) { return STICK.indexOf(f) < 0; });
    var out = ready ? rest.concat(STICK) : rest;
    var same = out.length === features.length && out.every(function (f) { return features.indexOf(f) >= 0; });
    return same ? features : out;
  }

  function navMode(width) { return width < 720 ? 'bar' : 'rail'; }

  function hex6(v) {
    var m = /^#?([0-9a-f]{6})$/i.exec(String(v || ''));
    return m ? '#' + m[1].toUpperCase() : null;
  }
  function rgb(h) { var n = parseInt(h.slice(1), 16); return [n >> 16 & 255, n >> 8 & 255, n & 255]; }
  function toHex(c) { return '#' + c.map(function (x) { return ('0' + Math.round(x).toString(16)).slice(-2); }).join('').toUpperCase(); }
  function mix(a, b, f) { var x = rgb(a), y = rgb(b); return toHex([0, 1, 2].map(function (i) { return x[i] + (y[i] - x[i]) * f; })); }
  function lum(h) {
    return rgb(h).map(function (c) { c /= 255; return c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4); })
      .reduce(function (s, c, i) { return s + c * [0.2126, 0.7152, 0.0722][i]; }, 0);
  }
  function contrast(a, b) { var x = lum(a), y = lum(b); return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05); }

  // A GTi colour theme or skin's style fields (fill, light, dark, text, accent) as the eleven tokens (house style 3).
  // A bar or page the text cannot be read on (a light theme's "dark") becomes the fill a little darker.
  function skinFromTheme(th) {
    var card = hex6(th.fill) || NEO.card, txt = hex6(th.text) || NEO.txt, dark = hex6(th.dark) || NEO.bar;
    var bar = contrast(txt, dark) >= 4.5 ? dark : mix(card, '#000000', 0.12);
    if (contrast(txt, bar) < 4.5) bar = card;
    return { bg: bar, bar: bar, card: card, line: hex6(th.light) || NEO.line, txt: txt, dim: mix(txt, card, 0.4),
             accent: hex6(th.accent) || NEO.accent, ok: NEO.ok, warn: NEO.warn, ins: NEO.ins, danger: NEO.danger };
  }

  function skinPresets() {
    var out = [{ name: 'NEO', tokens: Object.assign({}, NEO) }];
    Object.keys(THEMES).forEach(function (n) { out.push({ name: n, tokens: skinFromTheme(THEMES[n]) }); });
    return out;
  }

  // The eleven CSS variables from tokens; a missing or bad one is NEO's.
  function applySkin(tokens, style) {
    TOKENS.forEach(function (k) { style.setProperty('--' + k, hex6(tokens && tokens[k]) || NEO[k]); });
  }

  // The drives this device reaches, from /status and /api/drive (FluxDrive: DF0, and DF1 when it is on), one card each:
  // the disk's name, one status line, write protect, and dimmed with the reason while the Amiga reads its drive.
  function keptLine(kept, lang) {
    if (kept === 'stick') return t('kept_stick', lang);
    if (kept === 'lost') return t('not_kept', lang);
    if (kept === 'keeping') return t('keeping', lang);
    if (kept === 'failed') return t('keep_failed', lang);
    return '';
  }
  // names: what the device calls its drives (/api/system/info drive_names; a Webby dongle: ['Gotek']), DF0 and DF1 when none.
  function driveCards(st, drive, lang, names) {
    st = st || {};
    var busy = !!(drive && drive.motor);
    var cards = [];
    function card(id, loaded, name, wp, kept, stored) {
      var line = keptLine(kept, lang) || (!loaded ? '' : stored === undefined ? Math.round((st.size || 0) / 1024) + ' KB' :
        stored ? t('stored', lang) + ' · 880 KB' : t('storing', lang));
      cards.push({ id: id, label: (names && names[id === 'DF1' ? 1 : 0]) || id, loaded: !!loaded, name: loaded ? name : t('no_disk', lang), wp: !!wp, line: line,
                   dimmed: busy, reason: busy ? t('amiga_busy', lang) : '' });
    }
    card('DF0', st.loaded, st.name, st.wp, st.df0 && st.df0.stick && st.df0.stick.kept, st.stored);
    if (st.df1 && st.df1.on) card('DF1', st.df1.mounted, st.df1.name, st.df1.wp, st.df1.stick && st.df1.stick.kept, st.df1.stored);
    return cards;
  }

  // A job: one or more disks loaded into drives (kind 'load', to: each disk's drive name) or kept on the stick (kind 'keep'),
  // done disks finished, part the percent of the current one, state run | ok | fail (error). The bar in the row that
  // started it fills with jobPct and says jobText, until it is really done (Dimitri 6 Nov).
  function jobPct(j) {
    if (j.state === 'ok') return 100;
    return Math.round((j.done + (j.part || 0) / 100) / j.disks.length * 100);
  }
  function jobText(j, lang) {
    var n = j.disks.length, i = j.done + 1;
    if (j.state === 'fail') return t('job_fail', lang, [i, j.error || '']);
    if (j.state === 'run') return j.kind === 'keep' ? t('job_keep', lang, [i, n]) : t('job_load', lang, [i, n, j.to[j.done]]);
    if (j.kind === 'keep') return n === 1 ? t('job_kept_1', lang) : t('job_kept_all', lang, [n]);
    var s = j.disks.map(function (d, k) { return t('job_in', lang, [k + 1, j.to[k]]); }).join(', ');
    return s.charAt(0).toUpperCase() + s.slice(1);
  }

  // A load into drive (0 or 1) would lose what the Amiga wrote to the disk there: a NAS disk not kept, or a stick disk
  // whose changes cannot go back now (no stick, another stick, its file gone...). Waiting changes go first (the drive's
  // replace hook writes them back), so they need no question.
  function replaceWarning(st, drive) {
    var d = st && (drive ? st.df1 : st.df0);
    var s = d && d.stick;
    if (!s) return false;
    if (s.kept === 'lost' || s.kept === 'keeping' || s.kept === 'failed') return true;
    return s.pending > 0 && ['none', 'on_stick', 'waiting'].indexOf(s.state) < 0;
  }

  // The Games tab: /api/games/list's games by title (no regard to case), those whose title has `search` in it.
  function gameTiles(list, search) {
    var q = String(search || '').toLowerCase();
    return ((list && list.games) || []).filter(function (g) { return !q || g.title.toLowerCase().indexOf(q) >= 0; })
      .sort(function (a, b) { return a.title.toLowerCase() < b.title.toLowerCase() ? -1 : a.title.toLowerCase() > b.title.toLowerCase() ? 1 : 0; });
  }

  function base(path) { var f = path.split('/').pop(); return f.replace(/\.adf$/i, ''); }
  function fileUrl(path) { return path ? '/api/usb/file?path=' + encodeURIComponent(path) : ''; }

  // A game's page: its cover and notes, its disks, which drive holds each (by the disk's name: a drive shows its file's
  // name without .adf), and the drives a disk can go into.
  function program(game, st, features) {
    st = st || {};
    var df0 = st.loaded ? String(st.name || '').toLowerCase() : null;
    var df1 = st.df1 && st.df1.on && st.df1.mounted ? String(st.df1.name || '').toLowerCase() : null;
    var inDrive = game.disks.map(function (d) {
      var b = base(d).toLowerCase();
      return b === df0 ? 'DF0' : b === df1 ? 'DF1' : null;
    });
    var only = df0Only(game.title);
    return { title: game.title, coverUrl: fileUrl(game.cover), nfoUrl: fileUrl(game.nfo), manualUrl: fileUrl(game.manual),
             disks: game.disks, names: game.disks.map(base), inDrive: inDrive, df0Only: only,
             targets: features.indexOf('df1') >= 0 && !only ? ['DF0', 'DF1'] : ['DF0'] };
  }

  // The NAS list: its first letters (a digit or anything else is '#'), the names under one letter, or all whose name has
  // `search` in it (a search looks over every letter), and a game folder's disks in disk order and its cover.
  function letterOf(name) {
    var c = String(name).charAt(0).toUpperCase();
    return c >= 'A' && c <= 'Z' ? c : '#';
  }
  function letters(names) {
    var seen = {};
    names.forEach(function (n) { seen[letterOf(n)] = 1; });
    return Object.keys(seen).sort();
  }
  function pickRows(names, letter, search) {
    var q = String(search || '').toLowerCase();
    if (q) return names.filter(function (n) { return n.toLowerCase().indexOf(q) >= 0; });
    if (!letter) return names.slice();
    return names.filter(function (n) { return letterOf(n) === letter; });
  }
  function natural(a, b) {
    return a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' });
  }
  function diskPlan(files) {
    var disks = files.filter(function (f) { return /\.adf$/i.test(f.name); }).map(function (f) { return f.name; }).sort(natural);
    var cover = files.filter(function (f) { return /\.(png|jpe?g)$/i.test(f.name); }).map(function (f) { return f.name; }).sort(natural)[0] || '';
    var nfo = files.filter(function (f) { return /\.nfo$/i.test(f.name); }).map(function (f) { return f.name; })[0] || '';
    return { disks: disks, cover: cover, nfo: nfo };
  }

  // Games that load from DF0 alone, every disk of them (Dimitri's list, 6 Nov 2026): no DF0+DF1, no Load DF1. Matched by
  // titleKey: without "(...)" tags, ", The" moved to the front, roman numbers as digits, letters and digits only.
  var DF0_ONLY = [
    "A.G.E. - Advanced Galactic Empire", "A.M.C. - Astro Marine Corps (cracked version test)",
    "After Burner (Activision 2 disks version)", "Air Support", "Alcatraz", "Alien 3", "Alien Breed",
    "Alien Breed - Special Edition '92", "Alien Breed Tower Assault (OCS & AGA)",
    "Alien Breed 3D 2 (AGA 2MB version)(cracked version test)", "'Allo! 'Allo! Cartoon Fun!", "Altered Beast",
    "American Gladiators (cracked version test)", "Another World", "Apocalypse", "Aquanauts (cracked version test)",
    "Aquaventura", "Armalyte", "ATR - All Terrain Racing (cracked version test)", "B17 Flying Fortress", "B.A.T.",
    "B.A.T. II (cracked version test)", "Back to the Future - part III", "Bangkok Knights",
    "Barbarian II (Palace)(2 disks version)", "Bargon Attack", "Beavers", "Behind the Iron Gate",
    "Best of the Best - Championship Karate", "Beverly Hills Cop", "Black Sect (Fr)", "Blitz Tennis", "Blood Money",
    "Borobodur", "Brat", "Brian the Lion (OCS & AGA)", "Bubble Gun (cracked version test)",
    "Buffalo Bill's Wild West Show (cracked version test)", "Bump N Burn", "Cadaver", "California Games", "Campaign",
    "Carthage", "Celtic Legends", "Chuck Rock", "Chuck Rock 2", "City Cars (cracked version test)",
    "Club Football - The Manager", "COALA (ECS & AGA)", "Combo Racer", "Corporation", "Cosmic Spacehead",
    "Cougar Force (cracked version test)", "Crime Time (cracked version test)", "Curse of Enchantia",
    "Cyber Empires (cracked version test)", "Cygnus 8 (.adf's)", "Dalek Attack",
    "Damage - the sadistic Butchering of Humanity (cracked version test)", "DarkSpyre", "Denaris (2 disks version)",
    "Deuteros", "Devious Designs", "Discovery - in the steps of Columbus", "Double Agent", "Dragonflight",
    "Dyter 07 (2 disks version)(cracked version test)", "Eagle's Rider", "East vs West - Berlin 1948",
    "European Champions (Ocean)", "Exile (AGA)", "Exodus 3010 (cracked version test)", "Explora III (Fr)",
    "F1 Manager (cracked version test)", "F-19 Stealth Fighter", "Fantastic Dizzy (OCS)", "Fantastic Voyage",
    "Fatman - the Caped Consumer", "Fighter Bomber", "Fire and Ice", "First Samurai", "Flashback",
    "Forgotten Worlds", "Forest Dumb (Pl)(cracked version test)",
    "Forest Dumb Forever!!! (Pl)(cracked version test)", "Four Crystals of Trazere, The", "Frenetic", "Full Contact",
    "Fury of the Furries", "Future Space", "Fuzzball", "Galdregon's Domain", "Games, The - Summer Edition",
    "Games, The - Winter Edition", "Gateway Y (cracked version test)", "Garrison", "Garrison II",
    "Gem Stone Legend (cracked version test)", "Ghouls n Ghosts", "Gilbert - Escape from Drill", "Gobliiins",
    "Gobliins 2", "Goblins 3", "Golden Eagle (cracked version test)", "Grand National",
    "Grand Prix 500 2 (cracked version test)", "Guardian (AGA)", "Harlequin",
    "Hero's Quest - so you want to be a hero", "Hexuma (De)", "Highway Hawks", "Hoi", "Hollywood Poker Pro",
    "Home Alone", "Hot Rubber (.adf's)", "Hoyle's Official Book of Games Vol.1", "Humans, The", "Immortal, The",
    "Ishar (OCS & AGA)", "Ishar 2 (OCS)", "Island of Lost Hope", "Jim Power", "Jinks", "Joe & Mac Caveman Ninja",
    "Kaiser (De)(cracked version test)", "Katakis", "Kick Off 3 (OCS & AGA)", "Killing Cloud, The",
    "Leisure Suit Larry II", "Legend", "Legend of Ragnarok (cracked version test)",
    "Little Red Riding Hood (cracked version test)", "Lombard RAC Rally", "Lorna (cracked version test)",
    "Lotus III", "Master Axe (AGA)(cracked version test)", "Maupiti Island", "Mega Twins", "Metal Masters",
    "Miami Chase", "Miki (Pl)(cracked version test)", "Missiles Over Xerion", "Mobile Warfare (.adf's)",
    "Moonshine Racers", "Morph (OCS)", "Mortal Weapon (Pl)(cracked version test)", "MOT (Fr)(cracked version test)",
    "Mr. Blobby", "Murders in Space", "Mystere - Fuga dal Castello di Lockness (It)(.adf's)", "New York Warriors",
    "Ninja Remix", "Ninja Warriors, The", "Olympique de Marseille", "Operation Stealth", "Operation Wolf", "Ork",
    "Oscar", "Out Run Europa", "Outlands (cracked version test)", "Overkill (AGA)", "Panza Kick Boxing",
    "Paradise Lost (cracked version test)", "Pierre le Chef is... Out to Lunch (AGA)", "Pinball Hazard", "Pinkie",
    "Poltergeist (cracked version test)", "Power Drift", "Playdays", "Predator (cracked version test)", "Predator 2",
    "Premiere", "Prophecy I - the Viking Child", "Psyborg", "Push-Over", "Putty Squad (AGA)(cracked version test)",
    "Quik the Thunder Rabbit", "Rainbow Warriors (cracked version test)", "Ranx - the Videogame",
    "Realm of the Trolls (.adf's)", "Realms", "Retee 2!", "Robinson's Requiem (OCS)(.adf's)", "Robocop 2",
    "Rock'n Roll", "Rooster (Pl)(cracked version test)", "Rooster II (Pl)(cracked version test)", "Rubicon",
    "Safari Guns (cracked version test)", "Samurai - the Way of the Warrior", "Sanctuary (De)(cracked version test)",
    "Savage", "Second Samurai (OCS & AGA)", "Silly Putty", "Sink or Swim", "Shadow of the Beast",
    "Shadow of the Beast II", "Shadow Sorcerer", "Skarb Templariuszy (Pl)(cracked version test)", "Skidmarks",
    "Snapperazzi", "Spaceball", "Space Gun", "Space Quest III", "Spirit of Adventure (cracked version test)",
    "StarRay", "Steel Empire", "Stellar Crusade", "Strike Aces", "Stryx", "Sturmtruppen (cracked version test)",
    "Super Cars", "Super Cars II", "Super Cauldron", "Super League Manager (AGA)",
    "Super Methane Bros (cracked version test)", "Super Ski II", "Super Skweek", "Super Space Invaders",
    "Super Sport Challenge", "Super Taekwondo Master (Pl)(cracked version test)", "Superman - the Man of Steel",
    "Surf Ninjas (AGA)", "Swords and Galleons (cracked version test)", "Tactical Manager", "Tactical Manager 2",
    "Tennis Cup II", "Theatre of Death", "Thunder Jaws", "Thunderhawk AH-73M", "Time Race", "Time Soldier",
    "Tin Toy Adventure (AGA)(.adf's)", "Tiny Skweeks", "Tip Off", "Torvak the Warrior", "Transarctica (OCS)",
    "Transarctica (AGA)(cracked version test)", "Traps'n'Treasures", "Trolls (OCS & AGA)", "Turbo Out Run", "Tusker",
    "Ultima V", "Universe", "Venus the Flytrap", "VideoKid", "Vital Light", "Viz - the Game", "Walpurgis (PD game)",
    "Warm-Up (.adf's)", "Warriors of Releyne", "Ween - the Prophecy", "Wembley International Soccer (AGA)",
    "Western Games", "West Phaser (cracked version test)", "Whizz (OCS & AGA)", "Wings of Death",
    "Wizardry - Bane of the Cosmic Forge", "Wolfchild", "Wonder Dog", "Woody's World",
    "Worlds of Legend - Son of the Empire", "WWF European Rampage Tour", "WWF Wrestle Mania",
    "Xenon 2 (2 disks version)", "Xenophobe", "X-Out (2 disks version)", "Zool (OCS & AGA)", "Zool 2 (OCS)",
    // the NAS's own names for four of them (Dimitri, 6 Nov)
    "Jim Power in Mutant Planet", "Garrison 2 Legend Continues", "Discovery", "Damage"
  ];
  var ROMAN = { i: '1', ii: '2', iii: '3', iv: '4', v: '5', vi: '6', vii: '7', viii: '8', ix: '9', x: '10' };
  function titleKey(t) {
    var s = String(t || '').replace(/\([^)]*\)|\[[^\]]*\]/g, ' ').trim();
    var m = /^(.*?),\s*(the|a|an|der|die|das|le|la|les|il)\b(.*)$/i.exec(s);
    if (m) s = m[2] + ' ' + m[1] + m[3];
    // the NAS's spellings too: no "the" ("Shadow of Beast 2"), a series' first as a bare "1" ("Zool 1")
    var w = s.toLowerCase().split(/[^a-z0-9]+/).filter(function (x) { return x && x !== 'the'; })
      .map(function (x) { return ROMAN[x] || x; });
    if (w.length > 1 && w[w.length - 1] === '1') w.pop();
    return w.join('');
  }
  var df0Keys = null;
  function df0Only(title) {
    if (!df0Keys) { df0Keys = {}; DF0_ONLY.forEach(function (n) { df0Keys[titleKey(n)] = 1; }); }
    return !!df0Keys[titleKey(title)];
  }

  // The stick game whose disks hold `path` (a drive's source; FAT names in any case), or null.
  function gameForDisk(list, path) {
    if (!list || !list.games || !path) return null;
    var p = String(path).toLowerCase();
    for (var i = 0; i < list.games.length; i++)
      for (var d = 0; d < list.games[i].disks.length; d++) if (list.games[i].disks[d].toLowerCase() === p) return list.games[i];
    return null;
  }
  // The NAS folder of a disk loaded from it ("" for none).
  function nasFolder(path) {
    if (!path) return '';
    var i = String(path).lastIndexOf('/');
    return i > 0 ? path.slice(0, i) : '/';
  }

  // A favourite's key on the FluxDrive (/FAVS.TXT): a stick game by its folder (a loose one in the root by its first
  // disk), a file by its path, a NAS folder as "nas:" + its path.
  function favKey(game, nasPath) {
    if (game) return game.folder !== '/usb' ? game.folder : game.disks[0];
    return 'nas:' + nasPath;
  }

  // What is on an ADF (AmigaDOS): its name, OFS or FFS (NDOS for anything else), and its folders and files with their
  // sizes, read from the root block (880) through the header blocks' hash tables and hash chains. A broken disk cannot
  // make it loop: every block is read once.
  function adfDir(buf) {
    var b = new Uint8Array(buf);
    var none = { name: '', kind: 'NDOS', entries: [] };
    if (b.length < 901120 || b[0] !== 0x44 || b[1] !== 0x4f || b[2] !== 0x53) return none;
    var dv = new DataView(b.buffer, b.byteOffset, b.length);
    var u32 = function (blk, off) { return dv.getUint32(blk * 512 + off); };
    var s32 = function (blk, off) { return dv.getInt32(blk * 512 + off); };
    var name = function (blk) {
      var n = Math.min(b[blk * 512 + 432], 30), s = '';
      for (var i = 0; i < n; i++) s += String.fromCharCode(b[blk * 512 + 433 + i]);
      return s;
    };
    if (u32(880, 0) !== 2 || s32(880, 508) !== 1) return { name: '', kind: b[3] & 1 ? 'FFS' : 'OFS', entries: [] };
    var seen = {};
    function dir(blk, depth) {
      var out = [];
      for (var i = 0; i < 72; i++) {
        var h = u32(blk, 24 + 4 * i);
        while (h > 1 && h < 1760 && !seen[h]) {
          seen[h] = 1;
          var sec = s32(h, 508);
          if (sec === 2) out.push({ name: name(h), dir: true, entries: depth < 8 ? dir(h, depth + 1) : [] });
          else if (sec === -3) out.push({ name: name(h), size: u32(h, 324) });
          h = u32(h, 496);
        }
      }
      return out.sort(function (a, c) { return (a.dir === c.dir ? 0 : a.dir ? -1 : 1) || natural(a.name, c.name); });
    }
    seen[880] = 1;
    return { name: name(880), kind: b[3] & 1 ? 'FFS' : 'OFS', entries: dir(880, 0) };
  }

  // A path's levels for a breadcrumb: the stick's from /usb, the NAS's from /.
  function crumbs(path) {
    var parts = String(path || '/').split('/').filter(Boolean);
    var out = [];
    var acc = '';
    if (parts[0] !== 'usb') out.push({ name: '/', path: '/' });
    parts.forEach(function (p) { acc += '/' + p; out.push({ name: p, path: acc }); });
    return out.length ? out : [{ name: '/', path: '/' }];
  }

  // A skin file the user picks: a GTi .gts (magic GTS1, a reserved byte, the slot list version, the manifest's length
  // as 2 bytes little endian, then the manifest's JSON: the style fields and the name; GTi skins spec) or a colour theme
  // as JSON. Its name and its tokens (skinFromTheme), or null for anything else.
  function skinFromFile(buf, filename) {
    var b = new Uint8Array(buf);
    var json = null;
    try {
      if (b.length >= 8 && String.fromCharCode(b[0], b[1], b[2], b[3]) === 'GTS1') {
        var n = b[6] | b[7] << 8;
        json = JSON.parse(new TextDecoder().decode(b.subarray(8, 8 + n)));
      } else {
        json = JSON.parse(new TextDecoder().decode(b));
      }
    } catch (e) { return null; }
    if (!json || typeof json !== 'object' || !hex6(json.fill) || !hex6(json.text)) return null;
    var name = String(json.name || String(filename || 'SKIN').replace(/\.[^.]*$/, '')).toUpperCase().slice(0, 24);
    return { name: name, tokens: skinFromTheme(json) };
  }

  return { TOKENS: TOKENS, NEO: NEO, STRINGS: STRINGS, t: t, tabsFor: tabsFor, navMode: navMode, contrast: contrast,
           mix: mix, hex6: hex6, skinFromTheme: skinFromTheme, skinPresets: skinPresets, applySkin: applySkin,
           driveCards: driveCards, gameTiles: gameTiles, program: program, crumbs: crumbs, fileUrl: fileUrl,
           skinFromFile: skinFromFile, replaceWarning: replaceWarning, letters: letters, pickRows: pickRows,
           diskPlan: diskPlan, gameForDisk: gameForDisk, nasFolder: nasFolder, favKey: favKey, adfDir: adfDir, titleKey: titleKey,
           df0Only: df0Only, withStick: withStick, jobPct: jobPct, jobText: jobText };
})();
if (typeof module !== 'undefined') module.exports = AppLogic;
