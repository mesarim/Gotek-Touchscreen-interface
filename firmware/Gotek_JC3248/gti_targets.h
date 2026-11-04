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

inline GtLoadKeys gtLoadLayout(const CuRect& area, int n) {
  GtLoadKeys k = {}; k.n = 0; k.picker = false;
  if (n < 1) return k;
  if (n == 2) {
    CuRect c[2]; cuCols(area, 2, CU_GAP, c);
    k.key[0] = GtLoadKey{c[0], 0}; k.key[1] = GtLoadKey{c[1], 1}; k.n = 2; return k;
  }
  k.key[0] = GtLoadKey{area, n == 1 ? 0 : -1}; k.n = 1; k.picker = (n >= 3);
  return k;
}
