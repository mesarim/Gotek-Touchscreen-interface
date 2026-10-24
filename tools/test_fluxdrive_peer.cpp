// Host test for firmware/Gotek_JC3248/fluxdrive_peer.h: what the panel decides from a peer's board.
// Pure C++, no Arduino types. Run with tools/test_fluxdrive_peer.sh.
#include "fluxdrive_peer.h"
#include <cstdio>
#include <cstring>

static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { \
  ++g_run; \
  if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void test_fluxdrive_is_told_apart_by_its_beacon_board() {
  CHECK(fdIsFluxDrive("fluxdrive"));
  CHECK(!fdIsFluxDrive("supermini"));
  CHECK(!fdIsFluxDrive("zero"));
  CHECK(!fdIsFluxDrive("xiao"));
  CHECK(!fdIsFluxDrive(""));
  CHECK(!fdIsFluxDrive(nullptr));
  CHECK(!fdIsFluxDrive("fluxdrive2"));
  CHECK(!fdIsFluxDrive("FluxDrive"));
}

static void test_only_a_dd_adf_goes_to_a_fluxdrive() {
  CHECK(fdRefuseImage("fluxdrive", 901120) == nullptr);
  CHECK(fdRefuseImage("fluxdrive", 1802240) != nullptr);   // an HD ADF
  CHECK(fdRefuseImage("fluxdrive", 901119) != nullptr);
  CHECK(fdRefuseImage("fluxdrive", 901121) != nullptr);
  CHECK(fdRefuseImage("fluxdrive", 0) != nullptr);
  CHECK(std::strcmp(fdRefuseImage("fluxdrive", 1802240), "FluxDrive takes DD ADFs only (901,120 bytes)") == 0);
}

static void test_other_boards_are_never_refused_here() {
  CHECK(fdRefuseImage("supermini", 1802240) == nullptr);
  CHECK(fdRefuseImage("supermini", 901120) == nullptr);
  CHECK(fdRefuseImage("zero", 0) == nullptr);
  CHECK(fdRefuseImage("xiao", 2949120) == nullptr);
  CHECK(fdRefuseImage("", 1802240) == nullptr);
  CHECK(fdRefuseImage(nullptr, 1802240) == nullptr);
}

static void test_a_busy_fluxdrive_gets_two_minutes() {
  CHECK(fdStallLimitMs("fluxdrive") == 120000);
  CHECK(fdStallLimitMs("zero") == 15000);
  CHECK(fdStallLimitMs("supermini") == 15000);
  CHECK(fdStallLimitMs("") == 15000);
  CHECK(fdStallLimitMs(nullptr) == 15000);
  CHECK(fdReplyWaitMs("fluxdrive", 3000) == 120000);
  CHECK(fdReplyWaitMs("fluxdrive", 10000) == 120000);
  CHECK(fdReplyWaitMs("supermini", 3000) == 3000);
  CHECK(fdReplyWaitMs("xiao", 10000) == 10000);
  CHECK(fdReplyWaitMs("", 4000) == 4000);
  CHECK(fdReplyWaitMs(nullptr, 4000) == 4000);
}

// It sends no beacon while the Amiga's motor runs (12 s apart otherwise), so a game load outlasts the panel's 40 s.
static void test_a_fluxdrive_stays_listed_through_a_game_load() {
  CHECK(fdStaleMs("fluxdrive", 40000) == 180000);
  CHECK(fdStaleMs("fluxdrive", 40000) > fdStallLimitMs("fluxdrive"));
  CHECK(fdStaleMs("supermini", 40000) == 40000);
  CHECK(fdStaleMs("zero", 40000) == 40000);
  CHECK(fdStaleMs("", 40000) == 40000);
  CHECK(fdStaleMs(nullptr, 40000) == 40000);
}

static void test_the_busy_text() {
  CHECK(std::strcmp(fdBusyText(), "FluxDrive busy - the Amiga is reading") == 0);
}

static void test_a_fluxdrive_has_no_saves() {
  CHECK(!fdHasSaves("fluxdrive"));
  CHECK(fdHasSaves("supermini"));
  CHECK(fdHasSaves("zero"));
  CHECK(fdHasSaves("xiao"));
  CHECK(fdHasSaves(""));
  CHECK(fdHasSaves(nullptr));
}

static void test_the_device_word_in_messages() {
  CHECK(std::strcmp(fdDeviceWord("fluxdrive"), "FluxDrive") == 0);
  CHECK(std::strcmp(fdDeviceWord("supermini"), "dongle") == 0);
  CHECK(std::strcmp(fdDeviceWord(""), "dongle") == 0);
  CHECK(std::strcmp(fdDeviceWord(nullptr), "dongle") == 0);
}

// The ESP-NOW pairing reply names the board by number in pad[2]: 1 SuperMini, 2 Zero, 3 XIAO, 4 FluxDrive.
static void test_the_board_kind_from_the_pairing_reply() {
  CHECK(FD_KIND_FLUXDRIVE == 4);
  CHECK(std::strcmp(fdBoardForKind(4), "fluxdrive") == 0);
  CHECK(fdIsFluxDrive(fdBoardForKind(4)));
  for (int k = 0; k < 256; k++)
    if (k != 4) CHECK(!fdIsFluxDrive(fdBoardForKind((uint8_t)k)));
  CHECK(fdHasSaves(fdBoardForKind(1)));
  CHECK(!fdHasSaves(fdBoardForKind(4)));
}

// The home-Wi-Fi transport looks the paired device up as <prefix>-<mac4>.local.
static void test_the_mdns_prefix_of_a_paired_device() {
  CHECK(std::strcmp(fdHostPrefix(4), "fluxdrive") == 0);
  CHECK(std::strcmp(fdHostPrefix(1), "gotekomega") == 0);
  CHECK(std::strcmp(fdHostPrefix(2), "gotekomega") == 0);
  CHECK(std::strcmp(fdHostPrefix(3), "gotekomega") == 0);
  CHECK(std::strcmp(fdHostPrefix(0), "gotekomega") == 0);   // a pairing from before the kind was kept
  CHECK(std::strcmp(fdHostPrefix(255), "gotekomega") == 0);
}

// A FluxDrive that did not answer SET_NAME through the whole wait is still busy: the push that follows would only
// wait out the same game load a second time (about 4 minutes in all). The panel notes the address instead.
static void test_a_silent_set_name_means_busy() {
  CHECK(fdNameMeansBusy("fluxdrive", false, true));    // no answer, the wait ran out
  CHECK(!fdNameMeansBusy("fluxdrive", false, false));  // the connection closed early: not "busy"
  CHECK(!fdNameMeansBusy("fluxdrive", true, false));   // it answered
  CHECK(!fdNameMeansBusy("fluxdrive", true, true));
  CHECK(!fdNameMeansBusy("supermini", false, true));   // a dongle is never told "busy"
  CHECK(!fdNameMeansBusy("", false, true));
  CHECK(!fdNameMeansBusy(nullptr, false, true));
}

static void test_the_busy_memo_is_spent_once() {
  FdBusyMemo m;
  CHECK(!m.take("192.168.50.7", 1000, FD_BUSY_MEMO_MS));          // nothing noted yet
  m.note("192.168.50.7", 1000);
  CHECK(m.take("192.168.50.7", 1000 + 150, FD_BUSY_MEMO_MS));     // the push right after the name
  CHECK(!m.take("192.168.50.7", 1000 + 200, FD_BUSY_MEMO_MS));    // spent: the next send tries for real
}

static void test_the_busy_memo_is_for_one_address() {
  FdBusyMemo m;
  m.note("192.168.50.7", 5000);
  CHECK(!m.take("192.168.50.8", 5100, FD_BUSY_MEMO_MS));          // another device is not told "busy"
  CHECK(m.take("192.168.50.7", 5200, FD_BUSY_MEMO_MS));           // and the note was not spent by it
  m.note("192.168.50.7", 6000);
  CHECK(!m.take("", 6001, FD_BUSY_MEMO_MS));
  CHECK(!m.take(nullptr, 6001, FD_BUSY_MEMO_MS));
  CHECK(m.take("192.168.50.7", 6002, FD_BUSY_MEMO_MS));
}

static void test_the_busy_memo_expires() {
  FdBusyMemo m;
  m.note("192.168.50.7", 1000);
  CHECK(!m.take("192.168.50.7", 1000 + FD_BUSY_MEMO_MS + 1, FD_BUSY_MEMO_MS));   // a stale note never blocks a later send
  CHECK(!m.take("192.168.50.7", 1000 + 5, FD_BUSY_MEMO_MS));                      // and it is gone, not just late
  m.note("192.168.50.7", 1000);
  CHECK(m.take("192.168.50.7", 1000 + FD_BUSY_MEMO_MS, FD_BUSY_MEMO_MS));         // the limit itself still counts
}

static void test_the_busy_memo_survives_a_millis_wrap() {
  FdBusyMemo m;
  m.note("192.168.50.7", 0xFFFFFF00u);
  CHECK(m.take("192.168.50.7", 0x00000040u, FD_BUSY_MEMO_MS));    // 0x140 ms later, across the wrap
  m.note("192.168.50.7", 0xFFFFFF00u);
  CHECK(!m.take("192.168.50.7", 0xFFFFFF00u + FD_BUSY_MEMO_MS + 5u, FD_BUSY_MEMO_MS));
}

static void test_the_busy_memo_can_be_cleared_and_renoted() {
  FdBusyMemo m;
  m.note("192.168.50.7", 100);
  m.clear();                                                      // a new send starts: the old note must not leak in
  CHECK(!m.take("192.168.50.7", 101, FD_BUSY_MEMO_MS));
  m.note("192.168.50.7", 200);
  m.note("192.168.50.9", 210);                                    // one note only: the latest wins
  CHECK(!m.take("192.168.50.7", 211, FD_BUSY_MEMO_MS));
  CHECK(m.take("192.168.50.9", 212, FD_BUSY_MEMO_MS));
}

static void test_the_busy_memo_holds_a_long_address_safely() {
  FdBusyMemo m;
  char longip[200]; std::memset(longip, '1', sizeof(longip)); longip[sizeof(longip) - 1] = 0;
  m.note(longip, 10);                                             // no overrun, and it still matches itself
  CHECK(m.take(longip, 11, FD_BUSY_MEMO_MS));
}

// The multi-select result line is "<name>: <text>", centred without wrapping. A FluxDrive refusal is 44 characters long.
static void test_the_name_prefix_of_a_refusal_for_a_fluxdrive() {
  const int px_text_only = 44 * 6;                 // "FluxDrive takes DD ADFs only (901,120 bytes)" at 6 px
  const int px_with_name = (14 + 2 + 44) * 6;      // "fluxdrive-ab12: " in front
  CHECK(!fdKeepNamePrefix("fluxdrive", 1, px_with_name, 320));    // one target: the name says nothing new
  CHECK(!fdKeepNamePrefix("fluxdrive", 1, px_with_name, 480));
  CHECK(!fdKeepNamePrefix("fluxdrive", 3, px_with_name, 320));    // 360 px does not fit a 320 px screen: text alone
  CHECK(fdKeepNamePrefix("fluxdrive", 3, px_with_name, 480));     // fits a landscape screen: keep the name
  CHECK(fdKeepNamePrefix("fluxdrive", 2, px_text_only + 12, 320));
  CHECK(!fdKeepNamePrefix("fluxdrive", 2, 313, 320));             // the margin is 8 px each side
  CHECK(fdKeepNamePrefix("fluxdrive", 2, 312, 320));
}

static void test_every_other_board_keeps_its_message_as_before() {
  CHECK(fdKeepNamePrefix("supermini", 1, 360, 320));              // old behaviour, even when it clips
  CHECK(fdKeepNamePrefix("supermini", 3, 360, 320));
  CHECK(fdKeepNamePrefix("zero", 1, 10, 320));
  CHECK(fdKeepNamePrefix("", 1, 10, 320));
  CHECK(fdKeepNamePrefix(nullptr, 1, 10, 320));
}

// The fleet manager repaints when this changes. Only a FluxDrive carries state in its row worth watching: the Amiga's
// power and the disk it holds. Every other peer adds nothing, so a dongle's row repaints exactly when it did before.
static void test_the_row_signature_follows_a_fluxdrive_only() {
  char a[120], b[120];
  CHECK(fdRowSig("supermini", true, true, "Workbench", a, sizeof(a)) == 0 && a[0] == 0);
  CHECK(fdRowSig("zero", false, false, "", a, sizeof(a)) == 0 && a[0] == 0);
  CHECK(fdRowSig("", true, true, "x", a, sizeof(a)) == 0 && a[0] == 0);
  CHECK(fdRowSig(nullptr, true, true, "x", a, sizeof(a)) == 0 && a[0] == 0);

  CHECK(fdRowSig("fluxdrive", true, true, "Workbench 1.3", a, sizeof(a)) > 0);
  CHECK(fdRowSig("fluxdrive", true, true, "Workbench 1.3", b, sizeof(b)) > 0 && std::strcmp(a, b) == 0);   // steady: no repaint
  CHECK(fdRowSig("fluxdrive", false, true, "Workbench 1.3", b, sizeof(b)) > 0 && std::strcmp(a, b) != 0);  // Amiga off
  CHECK(fdRowSig("fluxdrive", true, false, "Workbench 1.3", b, sizeof(b)) > 0 && std::strcmp(a, b) != 0);  // ejected
  CHECK(fdRowSig("fluxdrive", true, true, "Workbench 2.0", b, sizeof(b)) > 0 && std::strcmp(a, b) != 0);   // another disk
  CHECK(fdRowSig("fluxdrive", true, true, "", b, sizeof(b)) > 0 && std::strcmp(a, b) != 0);
  CHECK(fdRowSig("fluxdrive", true, true, nullptr, b, sizeof(b)) > 0);
}

static void test_the_row_signature_fits_its_buffer() {
  char tiny[8];
  const size_t n = fdRowSig("fluxdrive", true, true, "a disk name much longer than the buffer", tiny, sizeof(tiny));
  CHECK(n < sizeof(tiny) && std::strlen(tiny) == n);             // cut, terminated, never past the end
  CHECK(fdRowSig("fluxdrive", true, true, "x", tiny, 0) == 0);    // no buffer: nothing written
}

// The FluxDrive the main screen speaks for (NOW PLAYING, the loaded marker, EJECT): the one the last disk of this boot
// went to while it is still heard, else the only FluxDrive heard, else none. Peers are identified by their beacon id.
static void test_the_target_is_where_the_last_disk_went() {
  const bool vis[] = { true, true, true };
  const bool fd[]  = { true, false, true };
  const char *ids[] = { "AA01", "BB02", "CC03" };
  CHECK(fdPickTarget(3, vis, fd, ids, "CC03") == 2);              // two FluxDrives heard: the one the disk went to
  CHECK(fdPickTarget(3, vis, fd, ids, "AA01") == 0);
}

static void test_without_a_usable_last_fling_the_only_fluxdrive_is_the_target() {
  const bool vis[] = { false, true, true };
  const bool fd[]  = { true, false, true };
  const char *ids[] = { "AA01", "BB02", "CC03" };
  CHECK(fdPickTarget(3, vis, fd, ids, "AA01") == 2);              // the last one went silent; one other FluxDrive is heard
  CHECK(fdPickTarget(3, vis, fd, ids, "") == 2);                  // no fling yet this boot
  CHECK(fdPickTarget(3, vis, fd, ids, nullptr) == 2);
  CHECK(fdPickTarget(3, vis, fd, ids, "BB02") == 2);              // the last disk went to a Webby: the FluxDrive
  CHECK(fdPickTarget(3, vis, fd, ids, "DD04") == 2);              // an id no longer in the roster
}

static void test_no_target_when_it_is_not_clear_which() {
  const bool vis2[] = { true, true, true };
  const bool fd2[]  = { true, false, true };
  const char *ids[] = { "AA01", "BB02", "CC03" };
  CHECK(fdPickTarget(3, vis2, fd2, ids, "") == -1);               // two FluxDrives and no fling: none
  CHECK(fdPickTarget(3, vis2, fd2, ids, "BB02") == -1);           // ... nor after a fling to the Webby
  const bool visGone[] = { false, true, true };
  const bool fd3[]     = { true, false, false };
  CHECK(fdPickTarget(3, visGone, fd3, ids, "AA01") == -1);        // its FluxDrive went silent, only Webbies left
  const bool visWebby[] = { false, true, false };
  CHECK(fdPickTarget(3, visWebby, fd2, ids, "BB02") == -1);       // only a Webby heard
  CHECK(fdPickTarget(0, nullptr, nullptr, nullptr, "AA01") == -1);
  const bool fdBoth[]  = { true, false, true };
  const bool visNone[] = { false, false, false };
  CHECK(fdPickTarget(3, visNone, fdBoth, ids, "AA01") == -1);     // nothing heard at all
}

// The beacon's "disk" against a name in the panel's library: blanks around it, case and an ".adf" at the end do not count.
static void test_the_same_disk_name() {
  CHECK(fdSameDiskName("Lemmings", "Lemmings"));
  CHECK(fdSameDiskName("LEMMINGS", "lemmings"));
  CHECK(fdSameDiskName("  Lemmings ", "Lemmings"));
  CHECK(fdSameDiskName("Lemmings", "\tLemmings  "));
  CHECK(fdSameDiskName("Lemmings.adf", "Lemmings"));               // a disk put in by FluxDrive's web page keeps its file name
  CHECK(fdSameDiskName("Lemmings", "lemmings.ADF "));
  CHECK(!fdSameDiskName("Lemmings", "Lemmings 2"));
  CHECK(!fdSameDiskName("Lemmings 2", "Lemmings"));
  CHECK(!fdSameDiskName("Lemmings", "Lemming"));
  CHECK(!fdSameDiskName("Lemmings.adf.adf", "Lemmings"));          // one extension, not any number
  CHECK(!fdSameDiskName("Workbench 1.3", "Workbench 1"));
}

static void test_an_empty_drive_name_matches_nothing() {
  CHECK(!fdSameDiskName("", "Lemmings"));
  CHECK(!fdSameDiskName("   ", "Lemmings"));
  CHECK(!fdSameDiskName("", ""));
  CHECK(!fdSameDiskName(" ", "  "));
  CHECK(!fdSameDiskName(".adf", ""));
  CHECK(!fdSameDiskName(".adf", ".adf"));                          // ".adf" alone is no name (FluxDrive calls it "DISK")
  CHECK(!fdSameDiskName(" .ADF", ".adf "));
  CHECK(!fdSameDiskName(nullptr, "Lemmings"));
  CHECK(!fdSameDiskName("Lemmings", nullptr));
  CHECK(!fdSameDiskName(nullptr, nullptr));
}

// The target must still be heard: a FluxDrive silent past its stale limit (switched off) stops being the target.
static void test_a_peer_is_heard_until_its_stale_limit() {
  CHECK(fdHeard(5000, 5000, 180000));
  CHECK(fdHeard(5000 + 179999, 5000, 180000));
  CHECK(!fdHeard(5000 + 180000, 5000, 180000));
  CHECK(!fdHeard(5000 + 900000, 5000, 180000));
  CHECK(fdHeard(0x00000100u, 0xFFFFFF00u, 180000));                  // across the millis wrap
  CHECK(fdHeard(0xFFFFFF80u, 0xFFFFFF00u, 180000));                  // seen + limit wraps, now not yet: still heard
  CHECK(!fdHeard(0xFFFFFF00u + 180000u, 0xFFFFFF00u, 180000));
}

// What the screen marks as loaded (green marker, EJECT label), for one game: gi, the name it is flung under, its disks.
// FdUiView: target = fleet mode with a FluxDrive target, tloaded/tdisk = its beacon; staged = a disk in this panel's
// RAM, sgame/sdisk = its game (-1 = none) and disk, sname = the name it was flung under.
static void test_ui_without_a_target_shows_what_is_staged_here() {
  FdUiView v = { false, false, "", true, 4, 2, "Lemmings" };
  int d = 9;
  CHECK(fdUiLoaded(v, 4, "Lemmings", 3, &d) && d == 2);
  CHECK(!fdUiLoaded(v, 5, "Lemmings", 1, &d) && d == -1);           // another game, even one of the same name
  v.staged = false;
  CHECK(!fdUiLoaded(v, 4, "Lemmings", 3, &d));
  v.tloaded = true; v.tdisk = "Lemmings";                             // a beacon counts only for the target
  CHECK(!fdUiLoaded(v, 4, "Lemmings", 3, &d));
}

static void test_ui_an_empty_fluxdrive_shows_nothing_loaded() {
  FdUiView v = { true, false, "Lemmings", true, 4, 0, "Lemmings" };
  int d = 9;
  CHECK(!fdUiLoaded(v, 4, "Lemmings", 1, &d) && d == -1);           // staged here, but the FluxDrive is empty
  v.tloaded = true; v.tdisk = "";                                     // "loaded" without a name: nothing matches
  CHECK(!fdUiLoaded(v, 4, "Lemmings", 1, &d));
  v.tdisk = nullptr;
  CHECK(!fdUiLoaded(v, 4, "Lemmings", 1, &d));
}

static void test_ui_the_fluxdrive_holds_the_disk_staged_here() {
  FdUiView v = { true, true, "lemmings ", true, 4, 2, "Lemmings" };
  int d = 9;
  CHECK(fdUiLoaded(v, 4, "Lemmings", 3, &d) && d == 2);             // the staged game, with the staged disk
  CHECK(!fdUiLoaded(v, 7, "Lemmings", 1, &d));                      // not a second game of the same name
  FdUiView u = { true, true, "My Disk", true, -1, -1, "My Disk" };    // a USR-DSK disk: staged, but no game
  CHECK(!fdUiLoaded(u, 5, "My Disk", 1, &d));
}

static void test_ui_a_stale_staging_gives_way_to_the_fluxdrive() {
  FdUiView v = { true, true, "Turrican", true, 4, 0, "Lemmings" };    // staged here Lemmings; the FluxDrive holds Turrican
  int d = 9;
  CHECK(!fdUiLoaded(v, 4, "Lemmings", 1, &d));                      // the staged game is not shown
  CHECK(fdUiLoaded(v, 6, "Turrican", 1, &d) && d == 0);             // the one the FluxDrive holds is
  FdUiView u = { true, true, "Turrican", false, 4, 0, "Turrican" };   // nothing staged, a game index and name left over
  CHECK(fdUiLoaded(u, 6, "Turrican", 1, &d) && d == 0);             // the leftovers do not count: the game of that name
}

static void test_ui_the_fluxdrive_holds_a_disk_not_staged_here() {
  FdUiView v = { true, true, "Monkey Island", false, -1, -1, "" };    // after a panel restart, a PC push
  int d = 9;
  CHECK(fdUiLoaded(v, 3, "monkey island", 1, &d) && d == 0);        // one disk: that one
  CHECK(fdUiLoaded(v, 3, "Monkey Island", 4, &d) && d == -1);       // four disks, all flung under the game's name
  CHECK(!fdUiLoaded(v, 2, "Monkey Island 2", 4, &d));
  v.tdisk = "Monkey Island.adf";                                      // put in by FluxDrive's web page
  CHECK(fdUiLoaded(v, 3, "Monkey Island", 1, &d));
  CHECK(fdUiLoaded(v, 3, "Monkey Island", 1, nullptr));
}

static void test_ui_a_name_is_matched_whole() {
  FdUiView v = { true, true, "Dr", false, -1, -1, "" };
  int d = 9;
  CHECK(!fdUiLoaded(v, 1, "Dr. Mario", 1, &d));
  CHECK(!fdUiLoaded(v, 2, "Dr. Doom's Revenge", 1, &d));
  v.tdisk = "Pinball Dreams Vol";
  CHECK(!fdUiLoaded(v, 3, "Pinball Dreams Vol. 1", 1, &d));
  v.tdisk = "Workbench 1";
  CHECK(!fdUiLoaded(v, 4, "Workbench 1.3", 1, &d));
  v.tdisk = "Dr. Mario";
  CHECK(fdUiLoaded(v, 1, "Dr. Mario", 1, &d));
  CHECK(!fdUiLoaded(v, 2, "Dr. Doom's Revenge", 1, &d));
}

// A tap on NOW PLAYING ejects the FluxDrive target's disk. A drag that starts there must not: the bar sits right under
// the list, so a flick of the bottom row begun a few px low starts in it. The bar here: LIST_X 150, NOW_Y 258, LIST_W
// 300, NOW_PLAY_H 22, so x 150..449, y 258..279.
static void test_a_tap_on_now_playing_ejects() {
  const FdRect bar = { 150, 258, 300, 22 };
  CHECK(fdBarEjects(true, 300, 268, bar, true, true, false));
  CHECK(fdBarEjects(true, 150, 258, bar, true, true, false));       // its top-left pixel
  CHECK(fdBarEjects(true, 449, 279, bar, true, true, false));       // its bottom-right pixel
}

static void test_a_tap_beside_now_playing_does_not_eject() {
  const FdRect bar = { 150, 258, 300, 22 };
  CHECK(!fdBarEjects(true, 149, 268, bar, true, true, false));      // the cover side
  CHECK(!fdBarEjects(true, 450, 268, bar, true, true, false));      // the A-Z bar
  CHECK(!fdBarEjects(true, 300, 257, bar, true, true, false));      // the list's bottom row
  CHECK(!fdBarEjects(true, 300, 280, bar, true, true, false));      // the bottom bar
}

static void test_only_a_tap_with_a_loaded_target_ejects() {
  const FdRect bar = { 150, 258, 300, 22 };
  CHECK(!fdBarEjects(true, 300, 268, bar, true, true, true));       // the finger moved: a drag, not a tap
  CHECK(!fdBarEjects(true, 300, 268, bar, false, true, false));     // no FluxDrive target
  CHECK(!fdBarEjects(true, 300, 268, bar, true, false, false));     // the FluxDrive is empty
  CHECK(!fdBarEjects(false, 300, 268, bar, true, true, false));     // no bar drawn (COMPACT)
}

// A FluxDrive holds an eject while the Amiga reads and runs it later: the panel says "busy" for that. Only a reply wait
// that ran out means it; a connection closed early without a reply is a plain failure, said as one.
static void test_only_a_full_wait_without_a_reply_is_busy() {
  CHECK(fdReplyRanOut(false, 3000, 3000));
  CHECK(fdReplyRanOut(false, 3017, 3000));
  CHECK(!fdReplyRanOut(false, 2999, 3000));                         // closed just before the end
  CHECK(!fdReplyRanOut(false, 12, 3000));                           // closed at once
  CHECK(!fdReplyRanOut(true, 3000, 3000));                          // it answered (a refusal) as the wait ended
  CHECK(!fdReplyRanOut(true, 5, 3000));
}

// The send buffer of a push to a FluxDrive. lwIP's default (TCP_SND_BUF, 5744 B = 4 segments of 1436) allows four
// segments in flight: 5744 B per round trip, 58 kB/s on a mesh with ~99 ms RTT. A FluxDrive's receive window takes 16
// segments. lwIP queues at most TCP_SND_QUEUELEN pbufs per connection (at least one per segment), so the buffer stays
// within (queuelen - 1) segments. 0 = leave lwIP's default alone, which every other board keeps.
static void test_a_fluxdrive_push_gets_up_to_16_segments_of_send_buffer() {
  CHECK(fdSendBufBytes("fluxdrive", 1436, 17) == 22976);            // 16 x 1436, as the brief computed it
  CHECK(fdSendBufBytes("fluxdrive", 1436, 16) == 21540);            // this build: TCP_SND_QUEUELEN (4*5744+1435)/1436 = 16
  CHECK(fdSendBufBytes("fluxdrive", 1436, 40) == 22976);            // never more than the 16 segments it can take in
}

static void test_every_other_board_keeps_the_default_send_buffer() {
  CHECK(fdSendBufBytes("supermini", 1436, 16) == 0);
  CHECK(fdSendBufBytes("zero", 1436, 16) == 0);
  CHECK(fdSendBufBytes("xiao", 1436, 16) == 0);
  CHECK(fdSendBufBytes("", 1436, 16) == 0);
  CHECK(fdSendBufBytes(nullptr, 1436, 16) == 0);
}

static void test_the_send_buffer_fits_what_lwip_can_queue() {
  CHECK(fdSendBufBytes("fluxdrive", 1436, 4) == 3 * 1436);          // a small queue: one pbuf short of it
  CHECK(fdSendBufBytes("fluxdrive", 1436, 2) == 1436);
  CHECK(fdSendBufBytes("fluxdrive", 1436, 1) == 0);                 // no room for even one segment: leave it
  CHECK(fdSendBufBytes("fluxdrive", 1436, 0) == 0);
  CHECK(fdSendBufBytes("fluxdrive", 0, 16) == 0);                   // no MSS: leave it
  for (uint32_t q = 0; q < 64; q++) CHECK(fdSendBufBytes("fluxdrive", 1436, q) <= (q ? (q - 1) * 1436 : 0));
  CHECK(fdSendBufBytes("fluxdrive", 9000, 17) <= 0xFFFF);           // snd_buf is 16 bits without window scaling
  CHECK(fdSendBufBytes("fluxdrive", 9000, 17) > 0);
}

// The guard rails on raising a connection's snd_buf: only a TCP connection that is ESTABLISHED, goes to the port the
// push was meant for and still sits exactly at lwIP's default (nothing queued yet); then by the difference only.
static void test_the_send_buffer_is_raised_by_the_difference() {
  const char *why = "x";
  CHECK(fdSndBufDelta(21540, true, true, 3333, 3333, 5744, 5744, &why) == 21540 - 5744 && why[0] == 0);
  CHECK(fdSndBufDelta(22976, true, true, 3333, 3333, 5744, 5744, &why) == 22976 - 5744);
  CHECK(fdSndBufDelta(21540, true, true, 3333, 3333, 5744, 5744, nullptr) == 15796);   // no reason wanted
}

static void test_the_send_buffer_is_left_alone_unless_all_is_as_expected() {
  const char *why = "";
  CHECK(fdSndBufDelta(21540, false, true, 3333, 3333, 5744, 5744, &why) == 0 && std::strcmp(why, "no TCP socket") == 0);
  CHECK(fdSndBufDelta(21540, true, false, 3333, 3333, 5744, 5744, &why) == 0 && std::strcmp(why, "not established") == 0);
  CHECK(fdSndBufDelta(21540, true, true, 80, 3333, 5744, 5744, &why) == 0 && std::strcmp(why, "another port") == 0);
  CHECK(fdSndBufDelta(21540, true, true, 3333, 3333, 5740, 5744, &why) == 0 && std::strcmp(why, "not at lwIP's default") == 0);  // 4 B queued
  CHECK(fdSndBufDelta(21540, true, true, 3333, 3333, 21540, 5744, &why) == 0);       // raised already
  CHECK(fdSndBufDelta(0, true, true, 3333, 3333, 5744, 5744, &why) == 0 && std::strcmp(why, "not wanted") == 0);   // any other board
  CHECK(fdSndBufDelta(5744, true, true, 3333, 3333, 5744, 5744, &why) == 0 && std::strcmp(why, "not wanted") == 0);
  CHECK(fdSndBufDelta(4000, true, true, 3333, 3333, 5744, 5744, &why) == 0);         // never lowered
  CHECK(fdSndBufDelta(70000, true, true, 3333, 3333, 5744, 5744, &why) == 0 && std::strcmp(why, "too large") == 0);   // 16-bit snd_buf
}

// "[wifi] joined" goes to gti.log at most every 30 s for the same access point; a different one is always logged.
static void test_a_join_is_logged_once_per_30_s_per_access_point() {
  const char *ap1 = "7C:10:C9:5A:25:F1", *ap2 = "7C:10:C9:5A:25:F2";
  CHECK(fdJoinLogDue(false, 1000, 0, "", ap1));                     // the first join of this boot
  CHECK(!fdJoinLogDue(true, 1000 + 29999, 1000, ap1, ap1));         // a reconnect to the same AP within 30 s
  CHECK(fdJoinLogDue(true, 1000 + 30000, 1000, ap1, ap1));
  CHECK(fdJoinLogDue(true, 1000 + 5, 1000, ap1, ap2));              // another AP: at once
  CHECK(fdJoinLogDue(true, 0x00000010u, 0xFFFFF000u, ap1, ap1) == false);   // across the millis wrap: 4112 ms
  CHECK(!fdJoinLogDue(true, 0xFFFFF100u, 0xFFFFF000u, ap1, ap1));   // last + 30 s wraps, now not yet: 256 ms
  CHECK(fdJoinLogDue(true, 1000 + 5, 1000, nullptr, ap1));
  CHECK(!fdJoinLogDue(true, 1000 + 5, 1000, nullptr, nullptr));
}

// The A/B switch FD_SNDBUF in CONFIG.TXT, read line by line as loadConfig reads it: KEY=VALUE, blanks around both, '#'
// for a comment. OFF, 0 and NO (any case) switch it off; any other value leaves it on. -1: the line is not this key.
static void test_the_send_buffer_switch_is_read_from_config_txt() {
  CHECK(fdCfgSwitch("FD_SNDBUF=OFF", "FD_SNDBUF") == 0);
  CHECK(fdCfgSwitch("  FD_SNDBUF = off ", "FD_SNDBUF") == 0);
  CHECK(fdCfgSwitch("FD_SNDBUF=0", "FD_SNDBUF") == 0);
  CHECK(fdCfgSwitch("FD_SNDBUF=No", "FD_SNDBUF") == 0);
  CHECK(fdCfgSwitch("FD_SNDBUF=ON", "FD_SNDBUF") == 1);
  CHECK(fdCfgSwitch("FD_SNDBUF=1", "FD_SNDBUF") == 1);
  CHECK(fdCfgSwitch("FD_SNDBUF=", "FD_SNDBUF") == 1);
  CHECK(fdCfgSwitch("FD_SNDBUF=OFFX", "FD_SNDBUF") == 1);
  CHECK(fdCfgSwitch("#FD_SNDBUF=OFF", "FD_SNDBUF") == -1);
  CHECK(fdCfgSwitch(" # FD_SNDBUF=OFF", "FD_SNDBUF") == -1);
  CHECK(fdCfgSwitch("FD_SNDBUFX=OFF", "FD_SNDBUF") == -1);
  CHECK(fdCfgSwitch("XFD_SNDBUF=OFF", "FD_SNDBUF") == -1);
  CHECK(fdCfgSwitch("FD_SNDBUF OFF", "FD_SNDBUF") == -1);
  CHECK(fdCfgSwitch("", "FD_SNDBUF") == -1);
  CHECK(fdCfgSwitch(nullptr, "FD_SNDBUF") == -1);
  CHECK(fdCfgSwitch("FD_SNDBUF=OFF", nullptr) == -1);
}

int main() {
  test_fluxdrive_is_told_apart_by_its_beacon_board();
  test_only_a_dd_adf_goes_to_a_fluxdrive();
  test_other_boards_are_never_refused_here();
  test_a_busy_fluxdrive_gets_two_minutes();
  test_a_fluxdrive_stays_listed_through_a_game_load();
  test_the_busy_text();
  test_a_fluxdrive_has_no_saves();
  test_the_device_word_in_messages();
  test_the_board_kind_from_the_pairing_reply();
  test_the_mdns_prefix_of_a_paired_device();
  test_a_silent_set_name_means_busy();
  test_the_busy_memo_is_spent_once();
  test_the_busy_memo_is_for_one_address();
  test_the_busy_memo_expires();
  test_the_busy_memo_survives_a_millis_wrap();
  test_the_busy_memo_can_be_cleared_and_renoted();
  test_the_busy_memo_holds_a_long_address_safely();
  test_the_name_prefix_of_a_refusal_for_a_fluxdrive();
  test_every_other_board_keeps_its_message_as_before();
  test_the_row_signature_follows_a_fluxdrive_only();
  test_the_row_signature_fits_its_buffer();
  test_the_target_is_where_the_last_disk_went();
  test_without_a_usable_last_fling_the_only_fluxdrive_is_the_target();
  test_no_target_when_it_is_not_clear_which();
  test_the_same_disk_name();
  test_an_empty_drive_name_matches_nothing();
  test_a_peer_is_heard_until_its_stale_limit();
  test_ui_without_a_target_shows_what_is_staged_here();
  test_ui_an_empty_fluxdrive_shows_nothing_loaded();
  test_ui_the_fluxdrive_holds_the_disk_staged_here();
  test_ui_a_stale_staging_gives_way_to_the_fluxdrive();
  test_ui_the_fluxdrive_holds_a_disk_not_staged_here();
  test_ui_a_name_is_matched_whole();
  test_a_tap_on_now_playing_ejects();
  test_a_tap_beside_now_playing_does_not_eject();
  test_only_a_tap_with_a_loaded_target_ejects();
  test_only_a_full_wait_without_a_reply_is_busy();
  test_a_fluxdrive_push_gets_up_to_16_segments_of_send_buffer();
  test_every_other_board_keeps_the_default_send_buffer();
  test_the_send_buffer_fits_what_lwip_can_queue();
  test_the_send_buffer_is_raised_by_the_difference();
  test_the_send_buffer_is_left_alone_unless_all_is_as_expected();
  test_a_join_is_logged_once_per_30_s_per_access_point();
  test_the_send_buffer_switch_is_read_from_config_txt();
  std::printf("%d checks, %d failed\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
