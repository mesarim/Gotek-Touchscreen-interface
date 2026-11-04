// Host test for firmware/Gotek_JC3248/gti_targets.h: the drives the screen can reach, and the strip / load-key layouts.
// Pure C++, no Arduino types. Run with tools/test_gti_targets.sh.
#include "gti_targets.h"
#include "calm_list.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
using std::min;

static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { \
  ++g_run; \
  if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static GtInputs wired(bool loaded, const char* name) {
  GtInputs in = {}; in.wireless = false; in.loaded = loaded; in.loaded_name = name; return in;
}
static GtPeerIn dongle(const char* name, bool loaded, const char* disk) {
  GtPeerIn p = {}; p.name = name; p.board = "supermini"; p.visible = true; p.heard = true; p.stored = true;
  p.loaded = loaded; p.disk = disk; return p;
}
static GtPeerIn flux(const char* name, bool loaded, const char* disk) {
  GtPeerIn p = dongle(name, loaded, disk); p.board = "fluxdrive"; return p;
}
static GtInputs fleet() {
  GtInputs in = {}; in.wireless = true; in.link_home = true; in.wifi_up = true; return in;
}

static void test_screen_in_a_gotek() {
  GtTarget t[GT_MAX_TARGETS];
  int n = gtBuildTargets(wired(true, "Lemmings"), nullptr, 0, t, GT_MAX_TARGETS);
  CHECK(n == 1);
  CHECK(t[0].kind == GT_WIRED); CHECK(std::strcmp(t[0].name, "Gotek") == 0);
  CHECK(std::strcmp(t[0].holds, "Lemmings") == 0);
  CHECK(t[0].linked && !t[0].wp && !t[0].storing && !t[0].unsaved);
  CHECK(t[0].peer == -1 && t[0].drive == 0);
  n = gtBuildTargets(wired(false, "stale"), nullptr, 0, t, GT_MAX_TARGETS);
  CHECK(n == 1 && t[0].holds[0] == 0);   // nothing loaded: the name is ignored
  n = gtBuildTargets(wired(true, nullptr), nullptr, 0, t, GT_MAX_TARGETS);
  CHECK(n == 1 && t[0].holds[0] == 0);
}

static void test_espnow_paired_dongle() {
  GtInputs in = {}; in.wireless = true; in.link_home = false; in.espnow_paired = true; in.espnow_linked = true;
  in.espnow_name = "Den"; in.loaded = true; in.loaded_name = "Turrican";
  GtTarget t[GT_MAX_TARGETS];
  int n = gtBuildTargets(in, nullptr, 0, t, GT_MAX_TARGETS);
  CHECK(n == 1 && t[0].kind == GT_DONGLE && std::strcmp(t[0].name, "Den") == 0 && std::strcmp(t[0].holds, "Turrican") == 0 && t[0].linked);
  in.espnow_linked = false; in.espnow_name = "";
  n = gtBuildTargets(in, nullptr, 0, t, GT_MAX_TARGETS);
  CHECK(n == 1 && std::strcmp(t[0].name, "Dongle") == 0 && !t[0].linked);
  in.espnow_paired = false;   // no dongle paired: loads go nowhere, no card
  CHECK(gtBuildTargets(in, nullptr, 0, t, GT_MAX_TARGETS) == 0);
}

static void test_fleet_without_a_peer_is_the_local_drive() {
  GtTarget t[GT_MAX_TARGETS];
  GtInputs in = fleet(); in.loaded = true; in.loaded_name = "Zool";
  int n = gtBuildTargets(in, nullptr, 0, t, GT_MAX_TARGETS);   // doLoadSelected: no peer visible -> the panel stays the drive
  CHECK(n == 1 && t[0].kind == GT_WIRED && std::strcmp(t[0].holds, "Zool") == 0);
  GtPeerIn hidden = dongle("X", true, "A"); hidden.visible = false;   // a locked dongle this screen may not use
  n = gtBuildTargets(in, &hidden, 1, t, GT_MAX_TARGETS);
  CHECK(n == 1 && t[0].kind == GT_WIRED);
  in.wifi_up = false; GtPeerIn d = dongle("D", false, "");
  n = gtBuildTargets(in, &d, 1, t, GT_MAX_TARGETS);   // Wi-Fi lost: not toFleet either
  CHECK(n == 1 && t[0].kind == GT_WIRED);
}

static void test_fleet_dongles() {
  GtInputs in = fleet(); in.loaded = true; in.loaded_name = "ignored in a fleet";
  GtPeerIn p[2] = { dongle("Hall", true, "Zool"), dongle("", false, "") };
  p[1].heard = false;
  GtTarget t[GT_MAX_TARGETS];
  int n = gtBuildTargets(in, p, 2, t, GT_MAX_TARGETS);
  CHECK(n == 2);
  CHECK(t[0].kind == GT_DONGLE && std::strcmp(t[0].name, "Hall") == 0 && std::strcmp(t[0].holds, "Zool") == 0 && t[0].linked && t[0].peer == 0);
  CHECK(t[1].kind == GT_DONGLE && std::strcmp(t[1].name, "Dongle") == 0 && t[1].holds[0] == 0 && !t[1].linked && t[1].peer == 1);
  // wp and storing belong to a FluxDrive; unsaved (dirty sectors) is reported by both
  p[0].wp = true; p[0].stored = false; p[0].dirty = 5;
  n = gtBuildTargets(in, p, 2, t, GT_MAX_TARGETS);
  CHECK(!t[0].wp && !t[0].storing && t[0].unsaved);
}

static void test_fleet_fluxdrive_df0_and_df1() {
  GtInputs in = fleet();
  GtPeerIn f = flux("FluxDrive", true, "Monkey Island - Disk 1");
  f.wp = true; f.stored = false; f.dirty = 3;
  f.df1 = true; f.loaded1 = true; f.disk1 = "Monkey Island - Disk 2"; f.wp1 = false; f.stored1 = true; f.dirty1 = 0;
  GtTarget t[GT_MAX_TARGETS];
  int n = gtBuildTargets(in, &f, 1, t, GT_MAX_TARGETS);
  CHECK(n == 2);
  CHECK(t[0].kind == GT_FLUX_DF0 && std::strcmp(t[0].name, "FluxDrive DF0") == 0 && t[0].drive == 0 && t[0].peer == 0);
  CHECK(std::strcmp(t[0].holds, "Monkey Island - Disk 1") == 0 && t[0].wp && t[0].storing && t[0].unsaved && t[0].linked);
  CHECK(t[1].kind == GT_FLUX_DF1 && std::strcmp(t[1].name, "FluxDrive DF1") == 0 && t[1].drive == 1 && t[1].peer == 0);
  CHECK(std::strcmp(t[1].holds, "Monkey Island - Disk 2") == 0 && !t[1].wp && !t[1].storing && !t[1].unsaved);
  f.df1 = false;   // DF1 off: only DF0, the other DF1 fields are not read
  n = gtBuildTargets(in, &f, 1, t, GT_MAX_TARGETS);
  CHECK(n == 1 && t[0].kind == GT_FLUX_DF0);
  f.df1 = true; f.loaded1 = false;   // DF1 on but empty
  n = gtBuildTargets(in, &f, 1, t, GT_MAX_TARGETS);
  CHECK(n == 2 && t[1].holds[0] == 0 && !t[1].storing);
  f.loaded = false; f.stored = false;   // an empty FluxDrive also says stored false: never STORING
  n = gtBuildTargets(in, &f, 1, t, GT_MAX_TARGETS);
  CHECK(!t[0].storing && t[0].holds[0] == 0);
  GtPeerIn named = flux("Amiga 500", true, "X");
  n = gtBuildTargets(in, &named, 1, t, GT_MAX_TARGETS);
  CHECK(std::strcmp(t[0].name, "Amiga 500 DF0") == 0);
}

static void test_mixed_fleet_and_limits() {
  GtInputs in = fleet();
  GtPeerIn p[3] = { dongle("Hall", true, "A"), flux("FD", true, "B"), dongle("Den", false, "") };
  p[1].df1 = true; p[1].loaded1 = true; p[1].disk1 = "C"; p[1].stored1 = true;
  GtPeerIn hid = dongle("Locked", true, "Z"); hid.visible = false;
  GtPeerIn all[4] = { p[0], hid, p[1], p[2] };
  GtTarget t[GT_MAX_TARGETS];
  int n = gtBuildTargets(in, all, 4, t, GT_MAX_TARGETS);
  CHECK(n == 4);   // Hall, FD DF0, FD DF1, Den; the locked one is skipped
  CHECK(t[0].peer == 0 && t[1].peer == 2 && t[2].peer == 2 && t[3].peer == 3);
  CHECK(t[2].kind == GT_FLUX_DF1);
  GtTarget small[2];
  CHECK(gtBuildTargets(in, all, 4, small, 2) == 2);   // never writes past max
  CHECK(gtBuildTargets(in, all, 4, small, 0) == 0);
}

static void test_long_names_are_cut_not_overrun() {
  char longname[200]; std::memset(longname, 'n', sizeof longname); longname[199] = 0;
  GtInputs in = wired(true, longname);
  GtTarget t[GT_MAX_TARGETS];
  gtBuildTargets(in, nullptr, 0, t, GT_MAX_TARGETS);
  CHECK(std::strlen(t[0].holds) == sizeof t[0].holds - 1);
  GtPeerIn p = dongle(longname, true, longname);
  gtBuildTargets(fleet(), &p, 1, t, GT_MAX_TARGETS);
  CHECK(std::strlen(t[0].name) == sizeof t[0].name - 1 && std::strlen(t[0].holds) == sizeof t[0].holds - 1);
}

static void test_strip_layout() {
  const CuRect area{8, 264, 464, 48};
  GtStrip s = gtStripLayout(area, 0);
  CHECK(s.cards == 0 && !s.more);
  s = gtStripLayout(area, 1);   // one wide card
  CHECK(s.cards == 1 && !s.more && s.card[0].x == 8 && s.card[0].w == 464 && s.card[0].h == 48);
  s = gtStripLayout(area, 2);   // two halves
  CHECK(s.cards == 2 && !s.more && s.card[0].w == 228 && s.card[1].x == 244 && cuRight(s.card[1]) == 472);
  s = gtStripLayout(area, 3);   // two cards and "+1"
  CHECK(s.cards == 2 && s.more && s.moreCount == 1);
  CHECK(s.moreKey.w == 72 && cuRight(s.moreKey) == 472 && s.moreKey.h == 48 && s.moreKey.y == 264);
  CHECK(cuRight(s.card[1]) == s.moreKey.x - 8 && s.card[1].x == cuRight(s.card[0]) + 8 && s.card[0].x == 8);
  CHECK(s.card[0].w + s.card[1].w + 72 + 16 == 464);
  s = gtStripLayout(area, 6);
  CHECK(s.cards == 2 && s.more && s.moreCount == 4);
  CHECK(GT_MORE_W >= CU_KEY);   // the +N key stays a touch target
}

static void test_load_layout() {
  const CuRect area{8, 200, 464, 48};
  GtLoadKeys k = gtLoadLayout(area, 0);
  CHECK(k.n == 0 && !k.picker);
  k = gtLoadLayout(area, 1);   // one full-width Load
  CHECK(k.n == 1 && !k.picker && k.key[0].target == 0 && k.key[0].r.x == 8 && k.key[0].r.w == 464);
  k = gtLoadLayout(area, 2);   // two keys, one per drive
  CHECK(k.n == 2 && !k.picker && k.key[0].target == 0 && k.key[1].target == 1);
  CHECK(k.key[0].r.w == 228 && k.key[1].r.x == 244 && cuRight(k.key[1].r) == 472);
  k = gtLoadLayout(area, 3);   // "Load into..."
  CHECK(k.n == 1 && k.picker && k.key[0].target == -1 && k.key[0].r.w == 464);
  k = gtLoadLayout(area, 9);
  CHECK(k.n == 1 && k.picker);
}

static void test_strip_hit() {
  GtStrip s = gtStripLayout(CuRect{8, 264, 464, 48}, 3);
  CuRect rs[3] = {s.card[0], s.card[1], s.moreKey};
  CHECK(cuHit(rs, 3, 20, 280) == 0);
  CHECK(cuHit(rs, 3, 300, 280) == 1);
  CHECK(cuHit(rs, 3, 450, 280) == 2);
}

static void test_frame_geometry() {
  CHECK(GT_TOPBAR_H == 40 && GT_CONTENT_TOP == 48);
  CHECK(GT_STRIP_H == 48 && GT_STRIP_Y == 320 - 8 - 48);
  CHECK(GT_CONTENT_BOTTOM == GT_STRIP_Y - 8);
  CuRect s = gtStripArea(480, 320);
  CHECK(s.x == 8 && s.y == 264 && s.w == 464 && s.h == 48);
  CuRect c = gtContentArea(480, 320);   // between the top bar and the strip
  CHECK(c.x == 0 && c.y == 48 && c.w == 480 && cuBottom(c) == 256);
}

static void test_top_bar() {
  GtTopBar b = gtTopBarLayout(480);
  CHECK(b.listTab.x == 8 && b.listTab.h == 48 && b.listTab.y == 0);   // drawn 40, touch 48
  CHECK(b.reelTab.x == cuRight(b.listTab) && b.reelTab.y == 0 && b.reelTab.h == 48);
  CHECK(b.gear.w == 48 && b.gear.h == 48 && cuRight(b.gear) == 472 && b.gear.y == 0);
  CHECK(b.count.x >= cuRight(b.reelTab) && cuRight(b.count) == b.gear.x - 8);
  CHECK(gtTopBarTap(b, 20, 20) == GT_TB_LIST);
  CHECK(gtTopBarTap(b, cuRight(b.listTab) + 5, 47) == GT_TB_REEL);
  CHECK(gtTopBarTap(b, 450, 10) == GT_TB_GEAR);
  CHECK(b.filter.h == 48 && b.filter.w >= 72 && b.filter.x >= cuRight(b.reelTab) && cuRight(b.filter) <= b.gear.x - 8);   // the reel's source key
  CHECK(gtTopBarTap(b, b.filter.x + 5, 20) == GT_TB_FILTER);
  CHECK(gtTopBarTap(b, 300, 20) == GT_TB_NONE);
  CHECK(gtTopBarTap(b, 20, 48) == GT_TB_NONE);   // below the touch row: the list's
  CHECK(gtTopBarTap(b, 450, 48) == GT_TB_NONE);
}

static void test_strip_tap() {
  GtStrip s2 = gtStripLayout(CuRect{8, 264, 464, 48}, 2);
  CHECK(gtStripTap(s2, 20, 280) == 0 && gtStripTap(s2, 300, 280) == 1);
  CHECK(gtStripTap(s2, 240, 280) == GT_STRIP_NONE);   // the gap
  CHECK(gtStripTap(s2, 20, 250) == GT_STRIP_NONE);
  GtStrip s1 = gtStripLayout(CuRect{8, 264, 464, 48}, 1);
  CHECK(gtStripTap(s1, 470, 300) == 0);
  GtStrip s5 = gtStripLayout(CuRect{8, 264, 464, 48}, 5);
  CHECK(gtStripTap(s5, 450, 290) == GT_STRIP_MORE);
  CHECK(gtStripTap(gtStripLayout(CuRect{8, 264, 464, 48}, 0), 100, 280) == GT_STRIP_NONE);
}

static void test_info_layout() {
  const bool ej[4] = {true, true, true, true};
  GtInfoLayout L = gtInfoLayout(480, 320, 2, 0, ej);
  CHECK(L.back.x == 8 && L.back.y >= 0 && L.back.h == 48 && L.back.w >= 48);
  CHECK(L.view.y >= cuBottom(L.back) + 4 && cuBottom(L.view) == 312);
  CHECK(L.cards == 2 && L.maxScroll == 0);
  CHECK(L.card[0].x == 8 && L.card[0].w == 464 && L.card[0].h == GT_INFO_CARD_H);
  CHECK(L.card[1].y == L.card[0].y + GT_INFO_CARD_H + 8);
  CHECK(L.eject[0].w == 112 && L.eject[0].h == 48 && cuRight(L.eject[0]) == 472 - 8);
  CHECK(L.eject[0].y == L.card[0].y + (GT_INFO_CARD_H - 48) / 2);
  CHECK(L.name[0].x == 16 && cuRight(L.name[0]) <= L.eject[0].x - 8);
  CHECK(gtInfoTap(L, L.back.x + 5, L.back.y + 5, ej) == GT_INFO_BACK);
  CHECK(gtInfoTap(L, L.eject[1].x + 5, L.eject[1].y + 5, ej) == GT_INFO_EJECT + 1);
  CHECK(gtInfoTap(L, L.name[0].x + 5, L.name[0].y + 2, ej) == GT_INFO_NAME + 0);
  CHECK(gtInfoTap(L, 300, 3, ej) == GT_INFO_NONE);
  // the tap area of a disk's name is the whole left part of its card: at least 48 px each way, not the 28 px of the text
  CHECK(L.tap[0].h >= 48 && L.tap[0].w >= 48 && L.tap[0].x == L.card[0].x && cuRight(L.tap[0]) <= L.eject[0].x - 8);
  CHECK(gtInfoTap(L, L.card[0].x + 3, L.card[0].y + 3, ej) == GT_INFO_NAME + 0);
  CHECK(gtInfoTap(L, L.card[0].x + 3, L.card[0].y + GT_INFO_CARD_H - 3, ej) == GT_INFO_NAME + 0);
  // an empty drive has no Eject: its key area does nothing
  const bool e2[4] = {true, false, false, false};
  GtInfoLayout L2 = gtInfoLayout(480, 320, 2, 0, e2);
  CHECK(gtInfoTap(L2, L2.eject[1].x + 5, L2.eject[1].y + 5, e2) == GT_INFO_NONE);
  CHECK(gtInfoTap(L2, L2.eject[0].x + 5, L2.eject[0].y + 5, e2) == GT_INFO_EJECT + 0);
  // three or more: scrolls
  GtInfoLayout L3 = gtInfoLayout(480, 320, 4, 0, ej);
  CHECK(L3.cards == 4 && L3.maxScroll == 4 * (GT_INFO_CARD_H + 8) - 8 - (L3.view.h));
  GtInfoLayout L3b = gtInfoLayout(480, 320, 4, 9999, ej);   // clamped
  CHECK(L3b.scroll == L3b.maxScroll && L3b.card[0].y == L3.card[0].y - L3.maxScroll);
  CHECK(gtInfoScrollFor(4, 2, L3.view.h) == min(2 * (GT_INFO_CARD_H + 8), L3.maxScroll));
  CHECK(gtInfoScrollFor(4, 0, L3.view.h) == 0);
  CHECK(gtInfoScrollFor(2, 1, L.view.h) == 0);   // everything fits: nothing to scroll
  // a card scrolled partly under the header cannot be tapped there
  GtInfoLayout L4 = gtInfoLayout(480, 320, 4, 60, ej);
  CHECK(L4.eject[0].y < L4.view.y);
  CHECK(gtInfoTap(L4, L4.eject[0].x + 5, L4.view.y - 2, ej) == GT_INFO_NONE);
  CHECK(gtInfoTap(L4, L4.eject[1].x + 5, L4.eject[1].y + 5, ej) == GT_INFO_EJECT + 1);
  GtInfoLayout L0 = gtInfoLayout(480, 320, 0, 0, ej);
  CHECK(L0.cards == 0 && L0.maxScroll == 0);
}

static void test_disk_tap() {
  // a tap on a disk key SELECTS the disk whenever more than one drive is reachable; it swaps only with exactly one, a game loaded, hot-swap on
  CHECK(gtDiskTap(1, true, true) == GT_DISK_SWAP);
  CHECK(gtDiskTap(1, false, true) == GT_DISK_SELECT);     // nothing loaded: just select
  CHECK(gtDiskTap(1, true, false) == GT_DISK_SELECT);     // HOTSWAP off: just select (the INSERT key does the swap)
  CHECK(gtDiskTap(2, true, true) == GT_DISK_SELECT);      // FluxDrive DF0 + DF1: never swap into DF0 behind the user's back
  CHECK(gtDiskTap(3, true, true) == GT_DISK_SELECT);
  CHECK(gtDiskTap(5, false, true) == GT_DISK_SELECT);
  CHECK(gtDiskTap(0, true, true) == GT_DISK_SELECT);      // no drive reachable
  // the counts the screen really has: a FluxDrive with DF1 on is two targets, with DF1 off one
  GtInputs in = fleet(); GtTarget t[GT_MAX_TARGETS];
  GtPeerIn f = flux("FluxDrive", true, "Monkey Island - Disk 1"); f.df1 = true; f.loaded1 = false;
  CHECK(gtDiskTap(gtBuildTargets(in, &f, 1, t, GT_MAX_TARGETS), true, true) == GT_DISK_SELECT);
  f.df1 = false;
  CHECK(gtDiskTap(gtBuildTargets(in, &f, 1, t, GT_MAX_TARGETS), true, true) == GT_DISK_SWAP);
  GtPeerIn two[2] = { dongle("A", true, "X"), dongle("B", false, "") };
  CHECK(gtDiskTap(gtBuildTargets(in, two, 2, t, GT_MAX_TARGETS), true, true) == GT_DISK_SELECT);
  CHECK(gtDiskTap(gtBuildTargets(in, two, 1, t, GT_MAX_TARGETS), true, true) == GT_DISK_SWAP);   // one dongle
  CHECK(gtDiskTap(gtBuildTargets(wired(true, "Lemmings"), nullptr, 0, t, GT_MAX_TARGETS), true, true) == GT_DISK_SWAP);   // the wired Gotek
}

static bool inside(const CuRect& r, int w, int h) { return r.x >= 0 && r.y >= 0 && cuRight(r) <= w && cuBottom(r) <= h && r.w > 0 && r.h > 0; }
static bool apart(const CuRect& a, const CuRect& b) { return cuRight(a) <= b.x || cuRight(b) <= a.x || cuBottom(a) <= b.y || cuBottom(b) <= a.y; }

static void test_program_layout() {
  for (int disks = 1; disks <= 3; disks += 2) {
    GtProgLayout L = gtProgLayout(480, disks);
    CHECK(L.hasStepper == (disks > 1));
    // everything sits between the header and the drive strip, nothing overlaps, nothing reaches the strip
    const CuRect all[] = {L.back, L.more, L.cover, L.details, L.status, L.loadArea};
    for (const CuRect& r : all) CHECK(inside(r, 480, GT_CONTENT_BOTTOM));
    if (L.hasStepper) CHECK(inside(L.stepper, 480, GT_CONTENT_BOTTOM));
    CHECK(L.back.h == 48 && L.more.h == 48 && L.more.w == 48 && L.loadArea.h == 48);
    CHECK(cuBottom(L.loadArea) == GT_CONTENT_BOTTOM && L.loadArea.x == 8 && L.loadArea.w == 464);
    CHECK(apart(L.back, L.more) && apart(L.cover, L.details) && apart(L.details, L.status) && apart(L.cover, L.status) && apart(L.status, L.loadArea) && apart(L.cover, L.loadArea));
    if (L.hasStepper) CHECK(apart(L.stepper, L.details) && apart(L.stepper, L.status) && L.stepper.h == 48 && apart(L.stepper, L.cover));
    CHECK(L.title.x >= cuRight(L.back) && cuRight(L.title) <= L.more.x);
    CHECK(L.status.h >= 16);
  }
  CHECK(gtProgLayout(480, 2).details.h < gtProgLayout(480, 1).details.h);   // the stepper takes the room of some text
  CHECK(gtProgLayout(480, 2).cover.w >= 176 && gtProgLayout(480, 2).cover.h >= 136);   // the cover of mockup 2
  CHECK(cuBottom(gtProgLayout(480, 2).cover) + 8 <= gtProgLayout(480, 2).loadArea.y);
  CHECK(gtProgLayout(480, 2).status.x >= cuRight(gtProgLayout(480, 2).cover));         // the status line sits right of the cover
  GtProgLayout L = gtProgLayout(480, 3);
  CHECK(gtProgTap(L, L.back.x + 5, L.back.y + 5) == GT_PR_BACK);
  CHECK(gtProgTap(L, L.more.x + 5, L.more.y + 5) == GT_PR_MORE);
  CHECK(gtProgTap(L, L.stepper.x + 5, L.stepper.y + 5) == GT_PR_PREV);
  CHECK(gtProgTap(L, cuRight(L.stepper) - 5, L.stepper.y + 5) == GT_PR_NEXT);
  CHECK(gtProgTap(L, L.stepper.x + L.stepper.w / 2, L.stepper.y + 5) == GT_PR_NONE);   // the text between the arrows
  CHECK(gtProgTap(L, L.cover.x + 5, L.cover.y + 5) == GT_PR_COVER);       // the cover opens full screen
  CHECK(gtProgTap(L, L.details.x + 5, L.details.y + 5) == GT_PR_DETAILS);  // the details text opens the description
  CHECK(L.cover.w >= 48 && L.cover.h >= 48 && L.details.w >= 48 && L.details.h >= 48);   // every touch area is at least 48 px
  GtProgLayout L1 = gtProgLayout(480, 1);
  CHECK(gtProgTap(L1, 200, 130) == GT_PR_DETAILS && L1.details.h >= 48);   // no stepper: the details take the room
  CHECK(gtProgTap(L1, 200, L1.status.y + 2) == GT_PR_NONE);                 // the status line is not a key
}

static void test_pick_layout() {
  const bool on[6] = {true, true, true, true, true, true};
  GtPickLayout L = gtPickLayout(480, 320, 4, 0);
  CHECK(L.cards == 4 && L.card[0].h == GT_PICK_CARD_H && L.card[1].y == L.card[0].y + GT_PICK_CARD_H + 8);
  CHECK(L.back.x == 8 && L.view.y >= cuBottom(L.back) + 4 && cuBottom(L.view) == 312);
  CHECK(L.maxScroll == 4 * (GT_PICK_CARD_H + 8) - 8 - L.view.h);
  CHECK(gtPickTap(L, L.back.x + 5, L.back.y + 5, on) == GT_PICK_BACK);
  CHECK(gtPickTap(L, 100, L.card[1].y + 10, on) == GT_PICK_ROW + 1);
  CHECK(gtPickTap(L, 100, L.card[0].y + GT_PICK_CARD_H + 3, on) == GT_PICK_NONE);   // the gap
  const bool off[6] = {true, false, true, true, true, true};
  CHECK(gtPickTap(L, 100, L.card[1].y + 10, off) == GT_PICK_NONE);                  // a drive that cannot be reached
  GtPickLayout S = gtPickLayout(480, 320, 4, 9999);
  CHECK(S.scroll == S.maxScroll);
  CHECK(gtPickTap(S, 100, S.view.y - 3, on) == GT_PICK_NONE);                        // under the header
  CHECK(gtPickLayout(480, 320, 2, 0).maxScroll == 0);
}

static void test_chip_words() {
  ClChip c = clRowChip(3, false, false, false, false, "", "disques", "charge");
  CHECK(std::strcmp(c.text, "3 disques") == 0);
  c = clRowChip(1, false, false, false, true, "", "disques", "charge");
  CHECK(std::strcmp(c.text, "charge") == 0);
}

int main() {
  test_screen_in_a_gotek();
  test_espnow_paired_dongle();
  test_fleet_without_a_peer_is_the_local_drive();
  test_fleet_dongles();
  test_fleet_fluxdrive_df0_and_df1();
  test_mixed_fleet_and_limits();
  test_long_names_are_cut_not_overrun();
  test_strip_layout();
  test_load_layout();
  test_strip_hit();
  test_frame_geometry();
  test_top_bar();
  test_strip_tap();
  test_info_layout();
  test_disk_tap();
  test_program_layout();
  test_pick_layout();
  test_chip_words();
  std::printf("%d checks, %d failed\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
