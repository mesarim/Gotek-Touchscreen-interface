// gti_targets.h - the drives the touchscreen can reach, and the calm layout's decisions that depend on how many.
//
// The drive strip at the bottom of every browsing screen and the load keys of the program screen both show "drives",
// whatever the setup: a screen wired into a Gotek, one ESP-NOW dongle, a fleet of dongles and FluxDrives (DF0, and DF1
// when its beacon says "df1":1). This file turns plain inputs into that list (gtBuildTargets) and the list's length
// into rectangles (gtStripLayout, gtLoadLayout). No Arduino types: tools/test_gti_targets.sh runs it on the PC.
//
// Which sketch state feeds which input (it mirrors where doLoadSelected sends a disk today):
//   GtInputs.wireless      g_wireless_mode          (false = STANDALONE: the screen is wired into the Gotek)
//   GtInputs.link_home     g_link_home              (true = home Wi-Fi fleet, false = ESP-NOW)
//   GtInputs.wifi_up       WiFi.status()==WL_CONNECTED
//   GtInputs.loaded/_name  g_loaded / (g_loaded_display or g_loaded_name): the disk the screen itself holds or last sent
//   GtInputs.espnow_*      espnowIsPaired(), the ESP-NOW link state, getDongleName(mac) of the paired dongle
//   GtPeerIn (one each)    g_pfPeers[i] (panel_fleet.h PfPeer): name=pfPeerLabel, board, visible=pfPeerVisible,
//                          heard=fdHeard(now, seen, fdStaleMs(board)), loaded/disk/wp/stored/dirty, and for a
//                          FluxDrive df1/loaded1/disk1/wp1/stored1/dirty1. Pass ALL peers in roster order; the
//                          Target's .peer is the index into that array (= g_pfPeers), .drive 0 or 1.
// What is reachable, in the order doLoadSelected decides it:
//   1. wireless + home Wi-Fi + connected + at least one visible peer ("toFleet"): one Target per visible dongle, two per
//      FluxDrive with df1 (DF0, DF1), one if not. The screen's own USB port is not a target then.
//   2. wireless + home Wi-Fi + connected + no visible peer, or Wi-Fi down: the panel stays the local drive: Wired.
//   3. wireless, ESP-NOW (not home Wi-Fi): the paired dongle, one Target; none while nothing is paired.
//   4. not wireless: Wired.
// "Wired" is "the Gotek": the screen cannot tell whether the Amiga sees it as DF0 or DF1, so it does not say.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "calm_ui.h"

enum GtKind { GT_WIRED = 0, GT_DONGLE = 1, GT_FLUX_DF0 = 2, GT_FLUX_DF1 = 3 };

#define GT_MAX_TARGETS 34   // 16 peers (PF_MAX_PEERS) x 2 drives + 1
#define GT_MORE_W      72   // the "+N" key of the drive strip
#define GT_NAME_MAX    40
#define GT_HOLDS_MAX   64

struct GtTarget {
  GtKind kind;
  char   name[GT_NAME_MAX];     // "Gotek", the dongle's name, "<FluxDrive name> DF0" / "... DF1"
  char   holds[GT_HOLDS_MAX];   // the disk in it, "" when empty
  bool   linked;                // heard (fleet), link up (ESP-NOW), always true for the wired Gotek
  bool   wp, storing, unsaved;  // write protect, "STORING..." (a FluxDrive's disk not in flash yet), unsaved sectors
  int    peer;                  // index into the peer array given to gtBuildTargets; -1 for Wired and the ESP-NOW dongle
  int    drive;                 // 0, or 1 for a FluxDrive's DF1
};

struct GtPeerIn {
  const char* name; const char* board;
  bool visible, heard;
  bool loaded; const char* disk; bool wp, stored; uint16_t dirty;
  bool df1, loaded1; const char* disk1; bool wp1, stored1; uint16_t dirty1;
};

struct GtInputs {
  bool wireless, link_home, wifi_up;
  bool loaded; const char* loaded_name;
  bool espnow_paired, espnow_linked; const char* espnow_name;
};

inline void gtCopy(char* dst, size_t cap, const char* src) {
  if (cap == 0) return;
  if (src == nullptr) src = "";
  size_t n = strlen(src); if (n > cap - 1) n = cap - 1;
  memcpy(dst, src, n); dst[n] = 0;
}

inline void gtSet(GtTarget& t, GtKind kind, const char* name, const char* suffix, bool has, const char* holds,
                  bool linked, bool wp, bool storing, bool unsaved, int peer, int drive) {
  t.kind = kind;
  gtCopy(t.name, sizeof t.name, name);
  if (suffix != nullptr) {   // "<name> DF0": the name is cut so the suffix always fits
    const size_t sl = strlen(suffix), nl = strlen(t.name), room = sizeof t.name - 1 - sl;
    if (nl > room) t.name[room] = 0;
    strcat(t.name, suffix);
  }
  gtCopy(t.holds, sizeof t.holds, has ? holds : "");
  t.linked = linked; t.wp = wp; t.storing = storing; t.unsaved = unsaved; t.peer = peer; t.drive = drive;
}

inline bool gtIsFlux(const GtPeerIn& p) { return p.board != nullptr && strcmp(p.board, "fluxdrive") == 0; }

// Fill out[] (at most max) with every drive the screen can reach (see the top for what that means); returns how many.
inline int gtBuildTargets(const GtInputs& in, const GtPeerIn* peers, int nPeers, GtTarget* out, int max) {
  int n = 0;
  auto room = [&]() { return n < max; };
  if (in.wireless && in.link_home && in.wifi_up && peers != nullptr) {
    int vis = 0;
    for (int i = 0; i < nPeers; i++) if (peers[i].visible) vis++;
    if (vis > 0) {
      for (int i = 0; i < nPeers; i++) {
        const GtPeerIn& p = peers[i];
        if (!p.visible) continue;
        const bool fx = gtIsFlux(p);
        const char* nm = (p.name != nullptr && p.name[0]) ? p.name : (fx ? "FluxDrive" : "Dongle");
        if (!room()) return n;
        gtSet(out[n++], fx ? GT_FLUX_DF0 : GT_DONGLE, nm, fx ? " DF0" : nullptr, p.loaded, p.disk, p.heard,
              fx && p.wp, fx && p.loaded && !p.stored, p.dirty > 0, i, 0);
        if (fx && p.df1) {
          if (!room()) return n;
          gtSet(out[n++], GT_FLUX_DF1, nm, " DF1", p.loaded1, p.disk1, p.heard, p.wp1, p.loaded1 && !p.stored1, p.dirty1 > 0, i, 1);
        }
      }
      return n;
    }
  }
  if (in.wireless && !in.link_home) {   // ESP-NOW: the paired dongle, if any
    if (!in.espnow_paired || !room()) return 0;
    const char* nm = (in.espnow_name != nullptr && in.espnow_name[0]) ? in.espnow_name : "Dongle";
    gtSet(out[n++], GT_DONGLE, nm, nullptr, in.loaded, in.loaded_name, in.espnow_linked, false, false, false, -1, 0);
    return n;
  }
  if (!room()) return 0;   // Wired: the Gotek the screen sits in (also a fleet with nobody in reach)
  gtSet(out[n++], GT_WIRED, "Gotek", nullptr, in.loaded, in.loaded_name, true, false, false, false, -1, 0);
  return n;
}

// ---- Layout decisions ---------------------------------------------------------------------------------------------

// The drive strip: 1 target = one wide card, 2 = two halves, 3 or more = two cards and a "+N" key of GT_MORE_W
// (N = n - 2; a tap on it shows all drives). The cards are the first two targets, in list order. 0 = nothing to draw.
struct GtStrip { CuRect card[2]; int cards; bool more; CuRect moreKey; int moreCount; };

inline GtStrip gtStripLayout(const CuRect& area, int n) {
  GtStrip s = {}; s.cards = 0; s.more = false; s.moreCount = 0;
  if (n < 1) return s;
  if (n == 1) { s.card[0] = area; s.cards = 1; return s; }
  if (n == 2) { cuCols(area, 2, CU_GAP, s.card); s.cards = 2; return s; }
  CuRect rest;
  cuTakeRight(area, GT_MORE_W, CU_GAP, &rest, &s.moreKey);
  cuCols(rest, 2, CU_GAP, s.card);
  s.cards = 2; s.more = true; s.moreCount = n - 2;
  return s;
}

// The load keys of the program screen: 1 target = one full-width Load, 2 = two keys side by side (target 0 and 1),
// 3 or more = one "Load into..." key (picker, target -1) that opens a list of every drive. 0 targets: no key.
struct GtLoadKey { CuRect r; int target; };
struct GtLoadKeys { GtLoadKey key[2]; int n; bool picker; };

inline GtLoadKeys gtLoadLayout(const CuRect& area, int n, bool stack = false) {
  GtLoadKeys k = {}; k.n = 0; k.picker = false;
  if (n < 1) return k;
  if (n == 2) {
    CuRect c[2]; if (stack) cuRows(area, 2, CU_GAP, c); else cuCols(area, 2, CU_GAP, c);   // portrait: one above the other
    k.key[0] = GtLoadKey{c[0], 0}; k.key[1] = GtLoadKey{c[1], 1}; k.n = 2; return k;
  }
  const CuRect one = stack ? CuRect{area.x, cuBottom(area) - CU_KEY, area.w, CU_KEY} : area;   // one key / the picker: the bottom row
  k.key[0] = GtLoadKey{one, n == 1 ? 0 : -1}; k.n = 1; k.picker = (n >= 3);
  return k;
}

// ---- The frame (A600-calm2): top bar, drive strip, Drive info -----------------------------------------------------
// Landscape 480 x 320: the top bar is drawn 40 px high but its touch row is 48; content from y 48 to 256; the drive
// strip (48 px) at y 264, 8 px above the bottom edge. Every browsing screen has the same three bands.
#define GT_TOPBAR_H        40
#define GT_CONTENT_TOP     48
#define GT_STRIP_H         48
#define GT_STRIP_Y         (320 - CU_MARGIN - GT_STRIP_H)      // 264
#define GT_CONTENT_BOTTOM  (GT_STRIP_Y - CU_GAP)               // 256

inline int gtStripY(int h) { return h - CU_MARGIN - GT_STRIP_H; }
inline int gtContentBottom(int h) { return gtStripY(h) - CU_GAP; }
inline CuRect gtStripArea(int w, int h) { return CuRect{CU_MARGIN, h - CU_MARGIN - GT_STRIP_H, w - 2 * CU_MARGIN, GT_STRIP_H}; }
inline CuRect gtContentArea(int w, int h) { return CuRect{0, GT_CONTENT_TOP, w, h - CU_MARGIN - GT_STRIP_H - CU_GAP - GT_CONTENT_TOP}; }

// Top bar: [List][Reel] tabs at the left, the count text to the right of them, the Settings key at the right edge.
// The rects are touch areas (48 high); the drawing inside is 40.
struct GtTopBar { CuRect listTab, reelTab, count, gear, filter; };   // filter: the reel's source key (ALL / FAV / MOST / the dice), shown in the reel only
enum { GT_TB_NONE = 0, GT_TB_LIST, GT_TB_REEL, GT_TB_GEAR, GT_TB_FILTER };
inline GtTopBar gtTopBarLayout(int w) {
  GtTopBar b;
  const int tw = w >= 400 ? 80 : 64;   // portrait (320): narrower tabs, the reel's source key still fits beside them
  b.listTab = CuRect{CU_MARGIN, 0, tw, CU_KEY};
  b.reelTab = CuRect{CU_MARGIN + tw, 0, tw, CU_KEY};
  b.gear    = CuRect{w - CU_MARGIN - CU_KEY, 0, CU_KEY, CU_KEY};
  const int cx = cuRight(b.reelTab);
  b.count   = CuRect{cx, 0, b.gear.x - CU_GAP - cx, CU_KEY};
  b.filter  = CuRect{cx + CU_GAP, 0, 88, CU_KEY};
  return b;
}
inline int gtTopBarTap(const GtTopBar& b, int x, int y) {
  if (cuContains(b.listTab, x, y)) return GT_TB_LIST;
  if (cuContains(b.reelTab, x, y)) return GT_TB_REEL;
  if (cuContains(b.gear, x, y))    return GT_TB_GEAR;
  if (cuContains(b.filter, x, y))  return GT_TB_FILTER;
  return GT_TB_NONE;
}

// A tap on the drive strip: the card (0 or 1), the "+N" key, or nothing.
#define GT_STRIP_NONE (-1)
#define GT_STRIP_MORE (-2)
inline int gtStripTap(const GtStrip& s, int x, int y) {
  for (int i = 0; i < s.cards; i++) if (cuContains(s.card[i], x, y)) return i;
  if (s.more && cuContains(s.moreKey, x, y)) return GT_STRIP_MORE;
  return GT_STRIP_NONE;
}

// Drive info: Back at the top left, one card per drive below it (caption, the disk's name at size 3, one status line
// at size 2, Eject at the right as a danger key 112 x 48), scrolling when more than two. Card i is at
// view.y + i * (card height + gap) - scroll. hasDisk[i] says drive i holds a disk (Eject and the name tap exist then).
#define GT_INFO_CARD_H 88
#define GT_INFO_EJECT_W 112
struct GtInfoLayout {
  CuRect back, view; int cards, scroll, maxScroll;
  CuRect card[GT_MAX_TARGETS], eject[GT_MAX_TARGETS], name[GT_MAX_TARGETS], tap[GT_MAX_TARGETS];   // name: where the text is drawn; tap: the touch area of the disk (the card left of Eject)
};
enum { GT_INFO_NONE = 0, GT_INFO_BACK = 1, GT_INFO_EJECT = 100, GT_INFO_NAME = 200 };   // + the card's index
inline int gtInfoMaxScroll(int n, int viewH) { const int t = n * (GT_INFO_CARD_H + CU_GAP) - CU_GAP - viewH; return t > 0 ? t : 0; }
// The scroll that brings drive `first` to the top of the view (clamped): the "+N" key opens Drive info at the third.
inline int gtInfoScrollFor(int n, int first, int viewH) {
  const int want = first * (GT_INFO_CARD_H + CU_GAP), mx = gtInfoMaxScroll(n, viewH);
  return want < mx ? want : mx;
}
inline GtInfoLayout gtInfoLayout(int w, int h, int n, int scroll, const bool* hasDisk) {
  GtInfoLayout L; memset(&L, 0, sizeof L);
  L.back = CuRect{CU_MARGIN, 4, 104, CU_KEY};
  const int vy = cuBottom(L.back) + CU_GAP;
  L.view = CuRect{CU_MARGIN, vy, w - 2 * CU_MARGIN, h - CU_MARGIN - vy};
  L.cards = n < 0 ? 0 : (n > GT_MAX_TARGETS ? GT_MAX_TARGETS : n);
  L.maxScroll = gtInfoMaxScroll(L.cards, L.view.h);
  L.scroll = scroll < 0 ? 0 : (scroll > L.maxScroll ? L.maxScroll : scroll);
  for (int i = 0; i < L.cards; i++) {
    const int y = L.view.y + i * (GT_INFO_CARD_H + CU_GAP) - L.scroll;
    L.card[i]  = CuRect{L.view.x, y, L.view.w, GT_INFO_CARD_H};
    L.eject[i] = CuRect{cuRight(L.card[i]) - CU_GAP - GT_INFO_EJECT_W, y + (GT_INFO_CARD_H - CU_KEY) / 2, GT_INFO_EJECT_W, CU_KEY};
    L.name[i]  = CuRect{L.card[i].x + CU_GAP, y + 26, L.eject[i].x - CU_GAP - (L.card[i].x + CU_GAP), 28};
    L.tap[i]   = CuRect{L.card[i].x, y, L.eject[i].x - CU_GAP - L.card[i].x, GT_INFO_CARD_H};
  }
  (void)hasDisk;
  return L;
}
inline int gtInfoTap(const GtInfoLayout& L, int x, int y, const bool* hasDisk) {
  if (cuContains(L.back, x, y)) return GT_INFO_BACK;
  if (!cuContains(L.view, x, y)) return GT_INFO_NONE;   // a card scrolled under the header cannot be tapped there
  for (int i = 0; i < L.cards; i++) {
    if (hasDisk == nullptr || !hasDisk[i]) continue;
    if (cuContains(L.eject[i], x, y)) return GT_INFO_EJECT + i;
    if (cuContains(L.tap[i], x, y))   return GT_INFO_NAME + i;
  }
  return GT_INFO_NONE;
}

// ---- A tap on a disk key (A600-calm3 fix) -------------------------------------------------------------------------------
// With more than one drive reachable (a FluxDrive with DF1 on, two dongles, ...) a disk key only SELECTS the disk: which drive it
// goes into is the INSERT key's business. The direct swap (eject, then insert the tapped disk) stays for exactly one drive
// (the wired Gotek, one dongle, a FluxDrive with DF1 off), with the game loaded and HOTSWAP on (the reel always passes true).
enum { GT_DISK_SELECT = 0, GT_DISK_SWAP = 1 };
inline int gtDiskTap(int targetCount, bool gameLoaded, bool hotswap) {
  return (targetCount == 1 && gameLoaded && hotswap) ? GT_DISK_SWAP : GT_DISK_SELECT;
}

// ---- The program screen (A600-calm4, mockup 2) ---------------------------------------------------------------------------
// Header: Back, the title, a "..." key (manual, .nfo, START FRESH). Then the cover on the left; at the right the details text and,
// for a multi-disk game, the disk stepper "< Disk 2 of 3 >"; one status line ("Disk 2: in DF1"); the load keys (gtLoadLayout over
// loadArea) in the last 48 px of the content; the drive strip below as on every screen. Rects are touch areas.
struct GtProgLayout { CuRect back, title, more, cover, details, stepper, status, loadArea; bool hasStepper; };
enum { GT_PR_NONE = 0, GT_PR_BACK, GT_PR_MORE, GT_PR_PREV, GT_PR_NEXT, GT_PR_COVER, GT_PR_DETAILS };
inline GtProgLayout gtProgLayout(int w, int h, int diskCount) {
  GtProgLayout L; memset(&L, 0, sizeof L);
  const int bottom = gtContentBottom(h);
  L.back  = CuRect{CU_MARGIN, 4, 104, CU_KEY};
  L.more  = CuRect{w - CU_MARGIN - CU_KEY, 4, CU_KEY, CU_KEY};
  L.title = CuRect{cuRight(L.back) + CU_GAP, 4, L.more.x - CU_GAP - (cuRight(L.back) + CU_GAP), CU_KEY};
  const int top = cuBottom(L.back) + CU_GAP;   // 60
  L.hasStepper = diskCount > 1;
  if (w >= 400) {   // landscape: the cover at the left, the details, stepper and status at its right, the keys side by side below
    L.cover = CuRect{CU_MARGIN, top, 176, 136};   // mockup 2
    const int rx = cuRight(L.cover) + CU_GAP, rw = w - CU_MARGIN - rx;
    if (L.hasStepper) {
      L.details = CuRect{rx, top, rw, 54};
      L.stepper = CuRect{rx, top + 60, rw, CU_KEY};
      L.status  = CuRect{rx, top + 60 + CU_KEY + 6, rw, 16};
    } else {
      L.details = CuRect{rx, top, rw, 108};
      L.status  = CuRect{rx, top + 108 + 6, rw, 16};
    }
    L.loadArea = CuRect{CU_MARGIN, bottom - CU_KEY, w - 2 * CU_MARGIN, CU_KEY};
  } else {          // portrait: everything stacked - the cover, the details, the stepper, the status, the keys one above the other
    const int fw = w - 2 * CU_MARGIN;
    L.cover = CuRect{CU_MARGIN, top, fw, 112};
    const int dy = cuBottom(L.cover) + 6;
    if (L.hasStepper) {
      L.details = CuRect{CU_MARGIN, dy, fw, 54};
      L.stepper = CuRect{CU_MARGIN, dy + 54 + 6, fw, CU_KEY};
      L.status  = CuRect{CU_MARGIN, dy + 54 + 6 + CU_KEY + 4, fw, 16};
    } else {
      L.details = CuRect{CU_MARGIN, dy, fw, 108};
      L.status  = CuRect{CU_MARGIN, dy + 108 + 4, fw, 16};
    }
    L.loadArea = CuRect{CU_MARGIN, bottom - (2 * CU_KEY + CU_GAP), fw, 2 * CU_KEY + CU_GAP};
  }
  return L;
}
inline int gtProgTap(const GtProgLayout& L, int x, int y) {
  if (cuContains(L.back, x, y)) return GT_PR_BACK;
  if (cuContains(L.more, x, y)) return GT_PR_MORE;
  if (L.hasStepper) {
    CuRect l, m, r; cuStepper(L.stepper, &l, &m, &r);
    if (cuContains(l, x, y)) return GT_PR_PREV;
    if (cuContains(r, x, y)) return GT_PR_NEXT;
  }
  if (cuContains(L.cover, x, y)) return GT_PR_COVER;       // the cover, full screen
  if (cuContains(L.details, x, y)) return GT_PR_DETAILS;   // the description text
  return GT_PR_NONE;
}

// ---- "Load into..." (mockup 10): one row per drive, what is in it now; a drive that cannot be reached is dimmed ---------------
#define GT_PICK_CARD_H 64
struct GtPickLayout { CuRect back, view; int cards, scroll, maxScroll; CuRect card[GT_MAX_TARGETS]; };
enum { GT_PICK_NONE = 0, GT_PICK_BACK = 1, GT_PICK_ROW = 100 };   // + the row's index
inline GtPickLayout gtPickLayout(int w, int h, int n, int scroll) {
  GtPickLayout L; memset(&L, 0, sizeof L);
  L.back = CuRect{CU_MARGIN, 4, 104, CU_KEY};
  const int vy = cuBottom(L.back) + CU_GAP;
  L.view = CuRect{CU_MARGIN, vy, w - 2 * CU_MARGIN, h - CU_MARGIN - vy};
  L.cards = n < 0 ? 0 : (n > GT_MAX_TARGETS ? GT_MAX_TARGETS : n);
  const int t = L.cards * (GT_PICK_CARD_H + CU_GAP) - CU_GAP - L.view.h;
  L.maxScroll = t > 0 ? t : 0;
  L.scroll = scroll < 0 ? 0 : (scroll > L.maxScroll ? L.maxScroll : scroll);
  for (int i = 0; i < L.cards; i++) L.card[i] = CuRect{L.view.x, L.view.y + i * (GT_PICK_CARD_H + CU_GAP) - L.scroll, L.view.w, GT_PICK_CARD_H};
  return L;
}
inline int gtPickTap(const GtPickLayout& L, int x, int y, const bool* enabled) {
  if (cuContains(L.back, x, y)) return GT_PICK_BACK;
  if (!cuContains(L.view, x, y)) return GT_PICK_NONE;
  for (int i = 0; i < L.cards; i++) if (cuContains(L.card[i], x, y)) return (enabled != nullptr && enabled[i]) ? GT_PICK_ROW + i : GT_PICK_NONE;
  return GT_PICK_NONE;
}

// ---- "Load into..." select mode (A600-aa3): one disk into several drives ---------------------------------------------------
// A tap on "Select several" turns the list into check marks; "Load (N)" loads the disk into every checked drive, each its own
// way (doLoadSelected with the drive named). Only drives of the fleet can be checked (peer >= 0), and only while heard: the
// wired Gotek and the ESP-NOW dongle are only ever the one target in the list (gtBuildTargets never mixes them with the
// fleet), so there is nothing to mix. One drive per FluxDrive: the same disk in DF0 and DF1 is two volumes of one name to
// AmigaDOS, so checking one of its drives unchecks the other.
inline bool gtSelectable(const GtTarget& t) { return t.peer >= 0 && t.linked; }
inline int gtSelectableCount(const GtTarget* t, int n) { int c = 0; for (int i = 0; i < n; i++) if (gtSelectable(t[i])) c++; return c; }
inline bool gtSelOffered(const GtTarget* t, int n) { return gtSelectableCount(t, n) >= 2; }   // "Select several" only when there is a choice
inline int gtSelCount(const bool* sel, int n) { int c = 0; for (int i = 0; i < n; i++) if (sel[i]) c++; return c; }
inline bool gtIsFluxKind(GtKind k) { return k == GT_FLUX_DF0 || k == GT_FLUX_DF1; }
// Toggle target i; false = not selectable (nothing changes).
inline bool gtSelToggle(const GtTarget* t, int n, bool* sel, int i) {
  if (i < 0 || i >= n || !gtSelectable(t[i])) return false;
  if (sel[i]) { sel[i] = false; return true; }
  sel[i] = true;
  if (gtIsFluxKind(t[i].kind))
    for (int j = 0; j < n; j++) if (j != i && t[j].peer == t[i].peer && gtIsFluxKind(t[j].kind)) sel[j] = false;
  return true;
}
// Drop the marks a rebuilt list no longer allows (a drive gone quiet).
inline void gtSelPrune(const GtTarget* t, int n, bool* sel) { for (int i = 0; i < n; i++) if (sel[i] && !gtSelectable(t[i])) sel[i] = false; }

// The picker with the select key: the key at the header's right; in select mode a "Load (N)" key along the bottom and the list
// above it. Tap codes as gtPickTap, plus the two keys (a zero-sized key is not there).
#define GT_PICK_SEL_W_WIDE   176   // "Select several" at size 2 (landscape)
#define GT_PICK_SEL_W_NARROW 96    // "Select" (portrait)
enum { GT_PICK_SELECT = 2, GT_PICK_LOAD = 3 };
inline GtPickLayout gtPickLayoutSel(int w, int h, int n, int scroll, bool offer, bool selecting, CuRect* selKey, CuRect* loadKey) {
  GtPickLayout L = gtPickLayout(w, h, n, 0);
  const int sw = w >= 400 ? GT_PICK_SEL_W_WIDE : GT_PICK_SEL_W_NARROW;
  *selKey  = (offer && !selecting) ? CuRect{w - CU_MARGIN - sw, 4, sw, CU_KEY} : CuRect{0, 0, 0, 0};
  *loadKey = selecting ? CuRect{CU_MARGIN, h - CU_MARGIN - CU_KEY, w - 2 * CU_MARGIN, CU_KEY} : CuRect{0, 0, 0, 0};
  if (selecting) L.view.h = loadKey->y - CU_GAP - L.view.y;
  const int t = L.cards * (GT_PICK_CARD_H + CU_GAP) - CU_GAP - L.view.h;
  L.maxScroll = t > 0 ? t : 0;
  L.scroll = scroll < 0 ? 0 : (scroll > L.maxScroll ? L.maxScroll : scroll);
  for (int i = 0; i < L.cards; i++) L.card[i] = CuRect{L.view.x, L.view.y + i * (GT_PICK_CARD_H + CU_GAP) - L.scroll, L.view.w, GT_PICK_CARD_H};
  return L;
}
inline int gtPickTapSel(const GtPickLayout& L, const CuRect& selKey, const CuRect& loadKey, int x, int y, const bool* enabled) {
  if (selKey.w > 0 && cuContains(selKey, x, y)) return GT_PICK_SELECT;
  if (loadKey.w > 0 && cuContains(loadKey, x, y)) return GT_PICK_LOAD;
  return gtPickTap(L, x, y, enabled);
}

// ---- Fleet (A600-aa3): one card per device, one row per drive ------------------------------------------------------------------
// Settings > Drives > Fleet. A card: the device's name (size 3), a grey line (kind and state, size 2), then one 48 px row per
// drive: a FluxDrive's DF0, and DF1 while its beacon says df1; a dongle's one drive. A row: the caption (DF0 / DF1; none for a
// dongle), the disk or "empty", STORING... while a FluxDrive's disk is not in flash yet, and on a FluxDrive a WP switch (inert
// without a disk). A dongle of a club build may carry a Claim / Unclaim key at the card's top right. A third drive is one more
// row: GT_FL_MAX_DRIVES.
#define GT_FL_MAX_CARDS  16   // PF_MAX_PEERS
#define GT_FL_MAX_DRIVES 3    // DF0, DF1, and room for DF2
#define GT_FL_HEAD_H     56   // the name (y + 8, 24 px) and the grey line (y + 34, 16 px)
#define GT_FL_WP_W       112  // "WP" and the switch: the touch area of a drive's write protect
#define GT_FL_CLAIM_W    128

struct GtFleetDrive { int drive; bool flux, has; const char* disk; bool wp, storing; };
// The drive rows of one device (a GtPeerIn as gtBuildTargets takes it); returns how many.
inline int gtFleetDrives(const GtPeerIn& p, GtFleetDrive* out, int max) {
  int n = 0; const bool fx = gtIsFlux(p);
  if (n < max) out[n++] = GtFleetDrive{0, fx, p.loaded, (p.loaded && p.disk) ? p.disk : "", fx && p.wp, fx && p.loaded && !p.stored};
  if (fx && p.df1 && n < max) out[n++] = GtFleetDrive{1, true, p.loaded1, (p.loaded1 && p.disk1) ? p.disk1 : "", p.wp1, p.loaded1 && !p.stored1};
  return n;
}

struct GtFleetCardIn { int rows; bool claim; bool wp[GT_FL_MAX_DRIVES]; };   // wp[k]: row k has a live WP switch
struct GtFleetCard { CuRect card, claim, row[GT_FL_MAX_DRIVES], wp[GT_FL_MAX_DRIVES]; int rows; bool hasClaim, wpOn[GT_FL_MAX_DRIVES]; };
struct GtFleetLayout { CuRect back, view; int cards, scroll, maxScroll, contentBottom; GtFleetCard c[GT_FL_MAX_CARDS]; };
enum { GT_FL_NONE = 0, GT_FL_BACK = 1, GT_FL_CLAIM = 100, GT_FL_WP = 1000 };   // CLAIM + card, WP + card * GT_FL_MAX_DRIVES + row
inline int gtFleetRows(int r) { return r < 0 ? 0 : (r > GT_FL_MAX_DRIVES ? GT_FL_MAX_DRIVES : r); }
inline int gtFleetCardH(int rows) { return GT_FL_HEAD_H + gtFleetRows(rows) * CU_KEY + CU_GAP; }
// tail: room under the last card for a line of text (claimed elsewhere), part of what scrolls.
inline GtFleetLayout gtFleetLayout(int w, int h, const GtFleetCardIn* in, int n, int scroll, int tail = 0) {
  GtFleetLayout L; memset(&L, 0, sizeof L);
  L.back = CuRect{CU_MARGIN, 4, 104, CU_KEY};
  const int vy = cuBottom(L.back) + CU_GAP;
  L.view = CuRect{CU_MARGIN, vy, w - 2 * CU_MARGIN, h - CU_MARGIN - vy};
  L.cards = n < 0 ? 0 : (n > GT_FL_MAX_CARDS ? GT_FL_MAX_CARDS : n);
  int total = tail;
  for (int i = 0; i < L.cards; i++) total += gtFleetCardH(in[i].rows) + (i ? CU_GAP : 0);
  L.maxScroll = total > L.view.h ? total - L.view.h : 0;
  L.scroll = scroll < 0 ? 0 : (scroll > L.maxScroll ? L.maxScroll : scroll);
  int y = L.view.y - L.scroll;
  for (int i = 0; i < L.cards; i++) {
    GtFleetCard& c = L.c[i];
    c.rows = gtFleetRows(in[i].rows); c.hasClaim = in[i].claim;
    c.card  = CuRect{L.view.x, y, L.view.w, gtFleetCardH(c.rows)};
    c.claim = CuRect{cuRight(c.card) - CU_GAP - GT_FL_CLAIM_W, y + 4, GT_FL_CLAIM_W, CU_KEY};
    for (int k = 0; k < c.rows; k++) {
      c.row[k]  = CuRect{c.card.x + CU_GAP, y + GT_FL_HEAD_H + k * CU_KEY, c.card.w - 2 * CU_GAP, CU_KEY};
      c.wp[k]   = CuRect{cuRight(c.row[k]) - GT_FL_WP_W, c.row[k].y, GT_FL_WP_W, CU_KEY};
      c.wpOn[k] = in[i].wp[k];
    }
    y += c.card.h + CU_GAP;
  }
  L.contentBottom = y - CU_GAP;
  return L;
}
inline int gtFleetTap(const GtFleetLayout& L, int x, int y) {
  if (cuContains(L.back, x, y)) return GT_FL_BACK;
  if (!cuContains(L.view, x, y)) return GT_FL_NONE;   // a card scrolled under the header cannot be tapped there
  for (int i = 0; i < L.cards; i++) {
    const GtFleetCard& c = L.c[i];
    if (!cuContains(c.card, x, y)) continue;
    if (c.hasClaim && cuContains(c.claim, x, y)) return GT_FL_CLAIM + i;
    for (int k = 0; k < c.rows; k++) if (c.wpOn[k] && cuContains(c.wp[k], x, y)) return GT_FL_WP + i * GT_FL_MAX_DRIVES + k;
    return GT_FL_NONE;
  }
  return GT_FL_NONE;
}

// A drive's write protect over FluxDrive's own web entry (the one its page uses): POST /api/disk/wp?drive=<d>&on=<0|1>.
// 200 = done (the body is its status); 400 / 403 / 409 = refused, the body says why ({"ok":false,"err":"..."}: no disk, DF1
// is off, no motor wire, locked); 503 = busy (the drive is in use; FluxDrive does not keep the change for later); anything
// else, or no answer (HTTPClient's codes are negative) = failed.
enum { GT_WP_DONE = 0, GT_WP_REFUSED, GT_WP_BUSY, GT_WP_FAILED };
inline int gtWpOutcome(int http) {
  if (http == 200) return GT_WP_DONE;
  if (http == 400 || http == 403 || http == 409) return GT_WP_REFUSED;
  if (http == 503) return GT_WP_BUSY;
  return GT_WP_FAILED;
}
inline int gtWpPath(char* buf, size_t cap, int drive, bool on) {
  if (cap == 0) return 0;
  const char* d = drive == 1 ? "1" : drive == 2 ? "2" : "0";
  const char* parts[] = {"/api/disk/wp?drive=", d, "&on=", on ? "1" : "0"};
  size_t n = 0;
  for (const char* p : parts) for (; *p && n + 1 < cap; p++) buf[n++] = *p;
  buf[n] = 0; return (int)n;
}
// The "err" string of FluxDrive's JSON answer ("" when there is none); a backslash escape keeps the character after it.
inline bool gtJsonErr(const char* body, char* out, size_t cap) {
  if (cap == 0) return false;
  out[0] = 0;
  if (body == nullptr) return false;
  const char* k = strstr(body, "\"err\"");
  if (k == nullptr) return false;
  k += 5;
  while (*k == ' ' || *k == ':') k++;
  if (*k != '"') return false;
  k++;
  size_t n = 0;
  while (*k && *k != '"' && n + 1 < cap) { if (*k == '\\' && k[1]) { out[n++] = k[1]; k += 2; continue; } out[n++] = *k++; }
  out[n] = 0;
  return n > 0;
}
