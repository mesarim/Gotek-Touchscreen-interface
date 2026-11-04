// Host test for firmware/Gotek_JC3248/gti_targets.h: the drives the screen can reach, and the strip / load-key layouts.
// Pure C++, no Arduino types. Run with tools/test_gti_targets.sh.
#include "gti_targets.h"
#include <cstdio>
#include <cstring>

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
  std::printf("%d checks, %d failed\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
