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

// v2 (writing): a FluxDrive takes writes and serves GET_SAVE as a Webby does, so it has saves like a dongle.
static void test_fd_has_saves_fluxdrive_true_webby_unchanged() {
  CHECK(fdHasSaves("fluxdrive"));
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
  CHECK(fdHasSaves(fdBoardForKind(4)));
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

// A FluxDrive row's WP key shows its beacon's "wp": a change of it repaints the row. Every other board still adds nothing.
static void test_the_row_signature_follows_write_protect() {
  char a[120], b[120];
  CHECK(fdRowSig("fluxdrive", true, true, "Workbench 1.3", a, sizeof(a), false) > 0);
  CHECK(fdRowSig("fluxdrive", true, true, "Workbench 1.3", b, sizeof(b), true) > 0 && std::strcmp(a, b) != 0);    // WP on
  CHECK(fdRowSig("fluxdrive", true, true, "Workbench 1.3", b, sizeof(b)) > 0 && std::strcmp(a, b) == 0);          // default: off
  CHECK(fdRowSig("fluxdrive", true, true, "Workbench 1.3", a, sizeof(a), true) > 0 &&
        fdRowSig("fluxdrive", true, true, "Workbench 1.3", b, sizeof(b), true) > 0 && std::strcmp(a, b) == 0);    // steady
  CHECK(fdRowSig("fluxdrive", true, false, "", a, sizeof(a), true) > 0 &&
        fdRowSig("fluxdrive", true, false, "", b, sizeof(b), false) > 0 && std::strcmp(a, b) != 0);              // empty drive too
  CHECK(fdRowSig("supermini", true, true, "Workbench", a, sizeof(a), true) == 0 && a[0] == 0);
  CHECK(fdRowSig(nullptr, true, true, "x", a, sizeof(a), true) == 0 && a[0] == 0);
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
  CHECK(!fdUiLoaded(v, 4, "Lemmings", 1, &d) && d == -1);           // staged here, but the bar shows nothing (both drives empty)
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
static void test_a_tap_on_now_playing_opens_the_info() {
  const FdRect bar = { 150, 258, 300, 22 };
  CHECK(fdBarOpensInfo(true, 300, 268, bar, true, true, false));
  CHECK(fdBarOpensInfo(true, 150, 258, bar, true, true, false));       // its top-left pixel
  CHECK(fdBarOpensInfo(true, 449, 279, bar, true, true, false));       // its bottom-right pixel
}

static void test_a_tap_beside_now_playing_does_not_open_it() {
  const FdRect bar = { 150, 258, 300, 22 };
  CHECK(!fdBarOpensInfo(true, 149, 268, bar, true, true, false));      // the cover side
  CHECK(!fdBarOpensInfo(true, 450, 268, bar, true, true, false));      // the A-Z bar
  CHECK(!fdBarOpensInfo(true, 300, 257, bar, true, true, false));      // the list's bottom row
  CHECK(!fdBarOpensInfo(true, 300, 280, bar, true, true, false));      // the bottom bar
}

static void test_only_a_tap_with_a_showing_target_opens_the_info() {
  const FdRect bar = { 150, 258, 300, 22 };
  CHECK(!fdBarOpensInfo(true, 300, 268, bar, true, true, true));       // the finger moved: a drag, not a tap
  CHECK(!fdBarOpensInfo(true, 300, 268, bar, false, true, false));     // no FluxDrive target
  CHECK(!fdBarOpensInfo(true, 300, 268, bar, true, false, false));     // the bar shows nothing (both drives empty)
  CHECK(!fdBarOpensInfo(false, 300, 268, bar, true, true, false));     // no bar drawn (COMPACT)
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

// ---- LAN saves: the record of what a single-target fling put in a FluxDrive (one line of /GTI/fdsaves.txt) ----------

static FdSaveRecord fdRec(const char* id, uint32_t uid, const char* sent, const char* orig, const char* tag) {
  FdSaveRecord r;
  std::memset(&r, 0, sizeof r);
  std::strcpy(r.id, id); r.uid = uid; std::strcpy(r.sent, sent); std::strcpy(r.orig, orig); std::strcpy(r.tag, tag);
  return r;
}
static bool fdRecSame(const FdSaveRecord& a, const FdSaveRecord& b) {
  return std::strcmp(a.id, b.id) == 0 && a.uid == b.uid && std::strcmp(a.sent, b.sent) == 0 &&
         std::strcmp(a.orig, b.orig) == 0 && std::strcmp(a.tag, b.tag) == 0;
}
// A path of exactly `len` characters: "/ADF/" then 'x's, ".adf" at the end.
static void fdLongPath(char* out, size_t len) {
  std::memset(out, 'x', len); std::memcpy(out, "/ADF/", 5); std::memcpy(out + len - 4, ".adf", 4); out[len] = 0;
}

static void test_fd_record_round_trip() {
  const FdSaveRecord a = fdRec("A1B2C3D4E5F6", 3735928559u, "/ADF/Lemmings/Lemmings.sav.80FE.adf", "/ADF/Lemmings/Lemmings.adf", "80FE");
  char line[400], la[400];
  const int n = fdRecordFormat(a, line, sizeof line);
  CHECK(n > 0 && (size_t)n == std::strlen(line));
  CHECK(std::strcmp(line, "A1B2C3D4E5F6\t3735928559\t80FE\t/ADF/Lemmings/Lemmings.sav.80FE.adf\t/ADF/Lemmings/Lemmings.adf") == 0);
  std::strcpy(la, line);
  FdSaveRecord b;
  std::memset(&b, 0x5A, sizeof b);
  CHECK(fdRecordParse(line, b) && fdRecSame(a, b));
  char lf[420];
  std::snprintf(lf, sizeof lf, "%s\n", line);                          // as read from the card: the newline does not count
  CHECK(fdRecordParse(lf, b) && fdRecSame(a, b));
  std::snprintf(lf, sizeof lf, "%s\r\n", line);                        // nor a CR an editor on a PC put before it
  CHECK(fdRecordParse(lf, b) && fdRecSame(a, b));

  // blanks, brackets, dots and UTF-8 in a name; the highest uid; the longest paths that fit (159 characters)
  const FdSaveRecord c = fdRec("0123456789AB", 0xFFFFFFFFu, "/ADF/Monkey Island (1990)(Lucasfilm)[cr SKR]/Disk 1 of 4.adf",
                               "/ADF/Caf\xC3\xA9 Ol\xC3\xA9 v1.2.adf", "00FF");
  CHECK(fdRecordFormat(c, line, sizeof line) > 0 && fdRecordParse(line, b) && fdRecSame(c, b));
  char p159[160]; fdLongPath(p159, 159);
  const FdSaveRecord d = fdRec("FFFFFFFFFFFF", 1, p159, p159, "ABCD");
  CHECK(fdRecordFormat(d, line, sizeof line) > 0 && fdRecordParse(line, b) && fdRecSame(d, b));

  // the output buffer: exactly enough, one byte short, none
  char exact[200];
  CHECK(fdRecordFormat(a, exact, (size_t)n + 1) == n && std::strcmp(exact, la) == 0);
  CHECK(fdRecordFormat(a, exact, (size_t)n) < 0 && exact[0] == 0);    // cut: nothing usable is left in it
  CHECK(fdRecordFormat(a, nullptr, 0) < 0);
}

static void test_fd_record_rejects_garbage() {
  const FdSaveRecord good = fdRec("A1B2C3D4E5F6", 42, "/ADF/G.adf", "/ADF/G.adf", "80FE");
  FdSaveRecord r = good;
  CHECK(fdRecordParse("A1B2C3D4E5F6\t42\t80FE\t/ADF/G.adf\t/ADF/G.adf", r) && fdRecSame(r, good));   // the reference line
  const char* bad[] = {
    "",
    "\n",
    "# FluxDrive save records",
    "A1B2C3D4E5F6\t42\t80FE\t/ADF/G.adf",                              // a field missing
    "A1B2C3D4E5F6\t42\t80FE\t/ADF/G.adf\t/ADF/G.adf\tmore",            // one too many
    "A1B2C3D4E5F6 42 80FE /ADF/G.adf /ADF/G.adf",                      // blanks are not the separator
    "\t42\t80FE\t/ADF/G.adf\t/ADF/G.adf",                              // no id
    "A1B2C3D4E5F60\t42\t80FE\t/ADF/G.adf\t/ADF/G.adf",                 // an id longer than a MAC
    "A1B2C3D4E5FG\t42\t80FE\t/ADF/G.adf\t/ADF/G.adf",                  // not hex
    "a1b2c3d4e5f6\t42\t80FE\t/ADF/G.adf\t/ADF/G.adf",                  // a beacon id is upper-case
    "A1B2C3D4E5F6\t0\t80FE\t/ADF/G.adf\t/ADF/G.adf",                   // uid 0: no disk, nothing to match
    "A1B2C3D4E5F6\t\t80FE\t/ADF/G.adf\t/ADF/G.adf",                    // no uid
    "A1B2C3D4E5F6\t4294967296\t80FE\t/ADF/G.adf\t/ADF/G.adf",          // past 32 bits
    "A1B2C3D4E5F6\t4294967338\t80FE\t/ADF/G.adf\t/ADF/G.adf",          // 2^32 + 42: not uid 42
    "A1B2C3D4E5F6\t18446744073709551658\t80FE\t/ADF/G.adf\t/ADF/G.adf",   // 2^64 + 42: not uid 42 either
    "A1B2C3D4E5F6\t99999999999\t80FE\t/ADF/G.adf\t/ADF/G.adf",
    "A1B2C3D4E5F6\t-1\t80FE\t/ADF/G.adf\t/ADF/G.adf",
    "A1B2C3D4E5F6\t+42\t80FE\t/ADF/G.adf\t/ADF/G.adf",
    "A1B2C3D4E5F6\t42x\t80FE\t/ADF/G.adf\t/ADF/G.adf",
    "A1B2C3D4E5F6\t 42\t80FE\t/ADF/G.adf\t/ADF/G.adf",
    "A1B2C3D4E5F6\t42\t80F\t/ADF/G.adf\t/ADF/G.adf",                   // a tag is 4 hex digits
    "A1B2C3D4E5F6\t42\t80FE0\t/ADF/G.adf\t/ADF/G.adf",
    "A1B2C3D4E5F6\t42\t80GE\t/ADF/G.adf\t/ADF/G.adf",
    "A1B2C3D4E5F6\t42\t80fe\t/ADF/G.adf\t/ADF/G.adf",                  // upper-case, as FluxDrive's save_tag
    "A1B2C3D4E5F6\t42\t\t/ADF/G.adf\t/ADF/G.adf",
    "A1B2C3D4E5F6\t42\t80FE\t\t/ADF/G.adf",                            // no file sent
    "A1B2C3D4E5F6\t42\t80FE\t/ADF/G.adf\t",                            // no original
    "A1B2C3D4E5F6\t42\t80FE\tADF/G.adf\t/ADF/G.adf",                   // a path from the card's root
    "A1B2C3D4E5F6\t42\t80FE\t/ADF/G.adf\tG.adf",
    "A1B2C3D4E5F6\t42\t80FE\t/ADF/G\x01.adf\t/ADF/G.adf",              // a control character
    "A1B2C3D4E5F6\t42\t80FE\t/ADF/G.adf\t/ADF/G\x7F.adf",
  };
  for (const char* line : bad) {
    FdSaveRecord keep = good;
    const bool parsed = fdRecordParse(line, keep);
    CHECK(!parsed && fdRecSame(keep, good));                           // refused, and the record left as it was
    if (parsed) std::printf("  accepted: \"%s\"\n", line);
  }
  CHECK(!fdRecordParse(nullptr, r) && fdRecSame(r, good));
  char p160[161]; fdLongPath(p160, 160);                               // one character more than the record holds
  char line[480];
  std::snprintf(line, sizeof line, "A1B2C3D4E5F6\t42\t80FE\t%s\t/ADF/G.adf", p160);
  CHECK(!fdRecordParse(line, r) && fdRecSame(r, good));
  std::snprintf(line, sizeof line, "A1B2C3D4E5F6\t42\t80FE\t/ADF/G.adf\t%s", p160);
  CHECK(!fdRecordParse(line, r) && fdRecSame(r, good));

  // the other way: a record that is not one is never written
  char out[400];
  FdSaveRecord x = good; x.uid = 0;                         CHECK(fdRecordFormat(x, out, sizeof out) < 0 && out[0] == 0);
  x = good; std::strcpy(x.tag, "80F");                      CHECK(fdRecordFormat(x, out, sizeof out) < 0);
  x = good; std::strcpy(x.id, "");                          CHECK(fdRecordFormat(x, out, sizeof out) < 0);
  x = good; std::strcpy(x.sent, "");                        CHECK(fdRecordFormat(x, out, sizeof out) < 0);
  x = good; std::strcpy(x.orig, "ADF/G.adf");               CHECK(fdRecordFormat(x, out, sizeof out) < 0);
  x = good; std::strcpy(x.sent, "/ADF/G\t2.adf");           CHECK(fdRecordFormat(x, out, sizeof out) < 0);   // would split the line
  x = good; std::strcpy(x.orig, "/ADF/G\n2.adf");           CHECK(fdRecordFormat(x, out, sizeof out) < 0);   // would make two lines
  x = good; std::memset(x.id, 'A', sizeof x.id);            CHECK(fdRecordFormat(x, out, sizeof out) < 0);   // no end to it
  x = good; std::memset(x.sent, 'x', sizeof x.sent); x.sent[0] = '/';
                                                            CHECK(fdRecordFormat(x, out, sizeof out) < 0);
}

// A record is made from what the fling knew, whole or not at all: a cut path would name another file.
static void test_fd_record_is_made_whole_or_not_at_all() {
  const FdSaveRecord good = fdRec("A1B2C3D4E5F6", 42, "/ADF/G.adf", "/ADF/G.adf", "80FE");
  FdSaveRecord r = good;
  CHECK(fdRecordSet(r, "0123456789AB", 7, "/ADF/H.sav.00FF.adf", "/ADF/H.adf", "00FF"));
  CHECK(fdRecSame(r, fdRec("0123456789AB", 7, "/ADF/H.sav.00FF.adf", "/ADF/H.adf", "00FF")));
  char p159[160]; fdLongPath(p159, 159);
  char p160[161]; fdLongPath(p160, 160);
  CHECK(fdRecordSet(r, "A1B2C3D4E5F6", 42, p159, p159, "80FE") && std::strcmp(r.sent, p159) == 0);
  r = good;
  CHECK(!fdRecordSet(r, "A1B2C3D4E5F6", 42, p160, "/ADF/G.adf", "80FE") && fdRecSame(r, good));
  CHECK(!fdRecordSet(r, "A1B2C3D4E5F6", 42, "/ADF/G.adf", p160, "80FE") && fdRecSame(r, good));
  CHECK(!fdRecordSet(r, "A1B2C3D4E5F6A", 42, "/ADF/G.adf", "/ADF/G.adf", "80FE") && fdRecSame(r, good));
  CHECK(!fdRecordSet(r, "A1B2C3D4E5F6", 42, "/ADF/G.adf", "/ADF/G.adf", "80FE0") && fdRecSame(r, good));
  CHECK(!fdRecordSet(r, "A1B2C3D4E5F6", 0, "/ADF/G.adf", "/ADF/G.adf", "80FE") && fdRecSame(r, good));   // no uid from the ack
  CHECK(!fdRecordSet(r, "A1B2C3D4E5F6", 42, "/ADF/G.adf", "/ADF/G.adf", "") && fdRecSame(r, good));     // an old FluxDrive: no tag
  CHECK(!fdRecordSet(r, "A1B2C3D4E5F6", 42, "", "/ADF/G.adf", "80FE") && fdRecSame(r, good));           // the test kit: no file
  CHECK(!fdRecordSet(r, nullptr, 42, "/ADF/G.adf", "/ADF/G.adf", "80FE") && fdRecSame(r, good));
  CHECK(!fdRecordSet(r, "A1B2C3D4E5F6", 42, nullptr, "/ADF/G.adf", "80FE") && fdRecSame(r, good));
  CHECK(!fdRecordSet(r, "A1B2C3D4E5F6", 42, "/ADF/G.adf", nullptr, "80FE") && fdRecSame(r, good));
  CHECK(!fdRecordSet(r, "A1B2C3D4E5F6", 42, "/ADF/G.adf", "/ADF/G.adf", nullptr) && fdRecSame(r, good));
}

// The panel fetches a FluxDrive's unsaved sectors only when they belong to the disk this panel sent it: the beacon's
// "dirty" > 0, a record for that FluxDrive, and the beacon's "load_id" equal to the uid its push ack gave that disk.
static void test_fd_should_fetch_needs_dirty_record_and_same_uid() {
  const FdSaveRecord r = fdRec("A1B2C3D4E5F6", 42, "/ADF/G.adf", "/ADF/G.adf", "80FE");
  CHECK(fdShouldFetch(1, 42, &r));
  CHECK(fdShouldFetch(1760, 42, &r));                                  // a whole disk
  CHECK(!fdShouldFetch(0, 42, &r));                                    // nothing unsaved
  CHECK(!fdShouldFetch(5, 43, &r));                                    // another disk in it now (a PC push, the web page)
  CHECK(!fdShouldFetch(5, 0, &r));                                     // no disk in it
  CHECK(!fdShouldFetch(5, 42, nullptr));                               // this panel never sent it a disk
  FdSaveRecord z = r; z.uid = 0;
  CHECK(!fdShouldFetch(5, 0, &z));                                     // an unknown uid matches nothing, not even "none"
}

// Before a swap (another disk flung to it, an eject) the panel fetches whenever the disk in it is the one this panel
// sent, whatever the dirty count says: that count comes only from a beacon, FluxDrive sends none while the Amiga's motor
// runs, and the panel zeroes it after its own fling. An empty fetch costs one round trip.
static void test_fd_fetch_before_swap_needs_a_matching_record_not_a_dirty_count() {
  const FdSaveRecord r = fdRec("A1B2C3D4E5F6", 42, "/ADF/G.adf", "/ADF/G.adf", "80FE");
  CHECK(fdFetchBeforeSwap(true, 42, &r));                              // no dirty count asked at all
  CHECK(!fdFetchBeforeSwap(true, 43, &r));                             // another disk in it now
  CHECK(!fdFetchBeforeSwap(true, 0, &r));                              // no uid known
  CHECK(!fdFetchBeforeSwap(true, 42, nullptr));                        // this panel never sent it a disk
  CHECK(!fdFetchBeforeSwap(false, 42, &r));                            // ejected: FluxDrive keeps the uid, but an ejected
                                                                       // disk's sectors cannot be fetched
  FdSaveRecord z = r; z.uid = 0;
  CHECK(!fdFetchBeforeSwap(true, 0, &z));
}

// Which file a fling sends to a target: a FluxDrive that is the only target gets its own save (Game.sav.<tag>.adf) when
// saves are on, it has a tag and that file exists; everything else gets the file staged here (the cable save or the
// image). Several targets get the staged file, as a Hivemind fan-out does.
static void test_fd_a_single_fluxdrive_target_gets_its_own_save() {
  const char* own = "/ADF/G.sav.80FE.adf";
  const char* staged = "/ADF/G.adf";
  CHECK(std::strcmp(fdFileToSend(1, true, true, "80FE", true, own, staged), own) == 0);
  CHECK(std::strcmp(fdFileToSend(2, true, true, "80FE", true, own, staged), staged) == 0);     // several targets
  CHECK(std::strcmp(fdFileToSend(0, true, true, "80FE", true, own, staged), staged) == 0);
  CHECK(std::strcmp(fdFileToSend(1, false, true, "80FE", true, own, staged), staged) == 0);    // a Webby
  CHECK(std::strcmp(fdFileToSend(1, true, false, "80FE", true, own, staged), staged) == 0);    // SAVES=OFF
  CHECK(std::strcmp(fdFileToSend(1, true, true, "", true, own, staged), staged) == 0);         // no tag in its beacon
  CHECK(std::strcmp(fdFileToSend(1, true, true, nullptr, true, own, staged), staged) == 0);
  CHECK(std::strcmp(fdFileToSend(1, true, true, "80F", true, own, staged), staged) == 0);      // not a tag
  CHECK(std::strcmp(fdFileToSend(1, true, true, "80FE0", true, own, staged), staged) == 0);
  CHECK(std::strcmp(fdFileToSend(1, true, true, "8GFE", true, own, staged), staged) == 0);
  CHECK(std::strcmp(fdFileToSend(1, true, true, "80FG", true, own, staged), staged) == 0);
  CHECK(std::strcmp(fdFileToSend(1, true, true, "80fe", true, own, staged), staged) == 0);     // upper-case, as save_tag
  CHECK(std::strcmp(fdFileToSend(1, true, true, "80FE", false, own, staged), staged) == 0);    // no own save yet
  CHECK(std::strcmp(fdFileToSend(1, true, true, "80FE", true, "", staged), staged) == 0);      // no name for it
  CHECK(std::strcmp(fdFileToSend(1, true, true, "80FE", true, nullptr, staged), staged) == 0);
  CHECK(fdFileToSend(1, true, true, "80FE", false, own, nullptr) == nullptr);                  // nothing staged: nothing
}

// ---- Panel controls (v2 writing): start fresh, write protect -------------------------------------------------------------

// A FluxDrive save tag names Game.sav.<tag>.adf: exactly 4 upper-case hex digits (its beacon's "tag", the softAP MAC's last
// two bytes). Anything else names no save.
static void test_fd_save_tag_is_four_upper_case_hex_digits() {
  CHECK(fdSaveTagOk("80FE"));
  CHECK(fdSaveTagOk("0000"));
  CHECK(fdSaveTagOk("A9F0"));
  CHECK(!fdSaveTagOk(""));                                             // an old FluxDrive: no tag in its beacon
  CHECK(!fdSaveTagOk(nullptr));
  CHECK(!fdSaveTagOk("80F"));
  CHECK(!fdSaveTagOk("80FE0"));
  CHECK(!fdSaveTagOk("80fe"));
  CHECK(!fdSaveTagOk("G0FE"));
  CHECK(!fdSaveTagOk("80FG"));
  CHECK(!fdSaveTagOk("8 FE"));
  CHECK(!fdSaveTagOk("80F/"));
  CHECK(!fdSaveTagOk("80F:"));
}

// START FRESH sends the original at once only to a FluxDrive that holds, right now, the disk this panel's record says it
// sent (the caller passes the record only when it names this game's file): the beacon says a disk is in, and its uid is
// the record's. Another disk, an ejected one (FluxDrive keeps its uid), no record: no swap, the save just goes.
static void test_fd_start_fresh_resends_only_when_that_disk_is_in() {
  const FdSaveRecord r = fdRec("A1B2C3D4E5F6", 42, "/ADF/G.sav.80FE.adf", "/ADF/G.adf", "80FE");
  CHECK(fdStartFreshResends(&r, 42, true));
  CHECK(!fdStartFreshResends(&r, 43, true));                           // record uid != beacon uid: another disk in it now
  CHECK(!fdStartFreshResends(&r, 42, false));                          // not loaded: ejected, it keeps the old uid
  CHECK(!fdStartFreshResends(&r, 0, true));                            // no uid in its beacon
  CHECK(!fdStartFreshResends(&r, 0, false));
  CHECK(!fdStartFreshResends(nullptr, 42, true));                      // no record of this game for it
  FdSaveRecord z = r; z.uid = 0;
  CHECK(!fdStartFreshResends(&z, 0, true));                            // an unknown uid matches nothing, not even "none"
  FdSaveRecord big = r; big.uid = 4294967295u;
  CHECK(fdStartFreshResends(&big, 4294967295u, true));                 // the whole 32 bits
  CHECK(!fdStartFreshResends(&big, 2147483647u, true));
}

// START FRESH's fling of the original skips the fetch every other swap makes first: the writes the FluxDrive holds belong
// to the save being thrown away (fetched, they would go into a new save of the game). Every other fling and every eject
// fetch exactly when fdFetchBeforeSwap says so.
static void test_fd_start_fresh_skips_the_fetch_before_its_swap() {
  const FdSaveRecord r = fdRec("A1B2C3D4E5F6", 42, "/ADF/G.sav.80FE.adf", "/ADF/G.adf", "80FE");
  CHECK(fdFetchBeforeSwap(true, 42, &r));                              // the fling it replaces would fetch...
  CHECK(!fdFetchFirstNeeded(true, true, 42, &r));                      // ...START FRESH does not
  CHECK(fdFetchFirstNeeded(false, true, 42, &r));                      // any other fling or eject: fetch first
  const bool ld[2] = { false, true };
  const uint32_t uids[3] = { 0, 42, 43 };
  const FdSaveRecord* recs[2] = { nullptr, &r };
  for (bool l : ld) for (uint32_t u : uids) for (const FdSaveRecord* p : recs) {
    CHECK(fdFetchFirstNeeded(false, l, u, p) == fdFetchBeforeSwap(l, u, p));
    CHECK(!fdFetchFirstNeeded(true, l, u, p));
  }
}

// What FluxDrive's answer to SET_WP means. 0x01: done (the disk is back in with that write protect). 0x00: no disk in it,
// nothing changed. No answer for the whole wait: busy - it holds commands while the Amiga reads and makes the change after
// the load (the beacon's "wp" shows it then). The connection closed early without an answer, or another byte: not done.
static void test_fd_wp_reply_meanings() {
  CHECK(fdWpFailText(0x01, false) == nullptr);
  CHECK(fdWpFailText(0x01, true) == nullptr);                          // an answer is an answer, however late
  CHECK(fdWpFailText(0x00, false) != nullptr && std::strcmp(fdWpFailText(0x00, false), "no disk in it") == 0);
  CHECK(fdWpFailText(-1, true) != nullptr && std::strcmp(fdWpFailText(-1, true), FD_WP_BUSY) == 0);
  CHECK(fdWpFailText(-1, false) != nullptr && std::strcmp(fdWpFailText(-1, false), "no reply") == 0);
  CHECK(fdWpFailText(0x02, false) != nullptr && std::strcmp(fdWpFailText(0x02, false), "refused") == 0);
  CHECK(fdWpFailText(0x05, false) != nullptr && std::strcmp(fdWpFailText(0x05, false), "refused") == 0);
  CHECK(fdWpFailText(0x00, true) != nullptr && std::strcmp(fdWpFailText(0x00, true), "no disk in it") == 0);
  CHECK(std::strcmp(FD_WP_BUSY, "no reply") != 0 && std::strcmp(FD_WP_BUSY, "no disk in it") != 0);
}

// START FRESH deletes a save file. A FluxDrive record whose base (`sent`, the file its Amiga was given) is that file must
// not be left pointing at nothing (its next fetch rebuilds from the base). Its base is its own save, the one that goes: the
// record goes (1). Otherwise the base becomes its own save (2) only when that FluxDrive is heard holding that very disk, or
// its state is unknown (not heard, STANDALONE) and it has no own save yet; else the record goes (1) and its own save stays
// as it is - another device's progress is never overwritten for a record that may be dead. Any other base: nothing (0).
// FAT names: case does not count; a path is compared whole.
static void test_fd_start_fresh_keeps_every_record_base() {
  const char* own = "/ADF/G.sav.80FE.adf";
  const char* cable = "/ADF/G.sav.adf";
  const int IT = FD_HOLDS_IT, OTHER = FD_HOLDS_OTHER, UNKNOWN = FD_HOLDS_UNKNOWN;
  CHECK(fdFreshRecordFate(own, own, own, IT, true) == 1);              // the target's own save was its base: dropped
  CHECK(fdFreshRecordFate(own, own, own, UNKNOWN, false) == 1);
  CHECK(fdFreshRecordFate(cable, own, cable, IT, false) == 2);         // the cable save was its base: becomes its own save
  CHECK(fdFreshRecordFate("/ADF/G.sav.00FF.adf", own, "/ADF/G.sav.00FF.adf", IT, false) == 2);   // another FluxDrive's own save
  CHECK(fdFreshRecordFate("/ADF/G.adf", own, cable, IT, true) == 0);   // the image: untouched
  CHECK(fdFreshRecordFate(own, own, cable, IT, true) == 0);            // its own save, not the one that goes
  CHECK(fdFreshRecordFate("/ADF/G.adf", own, own, IT, true) == 0);     // its own save goes, but its base is the image: fine
  CHECK(fdFreshRecordFate("/adf/g.SAV.ADF", own, cable, IT, false) == 2);   // case does not count
  CHECK(fdFreshRecordFate(own, "/adf/g.sav.80fe.ADF", "/ADF/G.SAV.80FE.adf", IT, true) == 1);
  CHECK(fdFreshRecordFate("/ADF/G.sav.adf.tmp", own, cable, IT, false) == 0);   // whole paths only
  CHECK(fdFreshRecordFate("/ADF/G.sav.ad", own, cable, IT, false) == 0);
  CHECK(fdFreshRecordFate(cable, own, "", IT, false) == 0);            // nothing goes
  CHECK(fdFreshRecordFate("", own, "", IT, false) == 0);
  CHECK(fdFreshRecordFate(nullptr, own, cable, IT, false) == 0);
  CHECK(fdFreshRecordFate(cable, nullptr, cable, IT, false) == 2);     // no own save name known: still keep the base
  CHECK(fdFreshRecordFate(cable, own, nullptr, IT, false) == 0);
  // fix round 1: whose state decides a rebase
  CHECK(fdFreshRecordFate(cable, own, cable, IT, true) == 2);          // heard holding that disk: rebase (own save -> .bak)
  CHECK(fdFreshRecordFate(cable, own, cable, OTHER, false) == 1);      // heard with another disk or none: the record is dead
  CHECK(fdFreshRecordFate(cable, own, cable, OTHER, true) == 1);
  CHECK(fdFreshRecordFate(cable, own, cable, UNKNOWN, false) == 2);    // not heard, nothing of its own to lose: rebase
  CHECK(fdFreshRecordFate(cable, own, cable, UNKNOWN, true) == 1);     // not heard, an own save there: it stays, record goes
  CHECK(fdFreshRecordFate(cable, own, cable, 7, false) == 1);          // a state it does not know: the safe way
  CHECK(fdFreshRecordFate("/ADF/G.adf", own, cable, OTHER, true) == 0);   // another base: never touched, whatever the state
  CHECK(fdFreshRecordFate("/ADF/G.adf", own, cable, UNKNOWN, true) == 0);
}

// What a record's FluxDrive is doing, from its beacon: not heard (or STANDALONE: no roster) = unknown; heard with a disk in
// it whose uid ("load_id") is the record's = it holds that disk; heard otherwise (another disk, ejected, no uid) = other.
static void test_fd_peer_holds_the_disk_of_its_record() {
  CHECK(fdPeerHolds(false, true, 42, 42) == FD_HOLDS_UNKNOWN);
  CHECK(fdPeerHolds(false, false, 0, 42) == FD_HOLDS_UNKNOWN);
  CHECK(fdPeerHolds(true, true, 42, 42) == FD_HOLDS_IT);
  CHECK(fdPeerHolds(true, true, 43, 42) == FD_HOLDS_OTHER);            // another disk in it
  CHECK(fdPeerHolds(true, false, 42, 42) == FD_HOLDS_OTHER);           // ejected: it keeps the uid
  CHECK(fdPeerHolds(true, true, 0, 0) == FD_HOLDS_OTHER);              // no uid: matches nothing
  CHECK(fdPeerHolds(true, true, 4294967295u, 4294967295u) == FD_HOLDS_IT);
  CHECK(FD_HOLDS_UNKNOWN != FD_HOLDS_IT && FD_HOLDS_IT != FD_HOLDS_OTHER && FD_HOLDS_UNKNOWN != FD_HOLDS_OTHER);
}

// START FRESH is offered in STANDALONE (wireless off; home Wi-Fi then only serves the web page) and in fleet mode (wireless
// over home Wi-Fi), never in ESP-NOW mode (wireless without home Wi-Fi): ESP-NOW behaves as before Task 8.
static void test_fd_start_fresh_is_offered_in_standalone_and_fleet_mode_only() {
  CHECK(fdFreshOffered(false, false));                                 // STANDALONE
  CHECK(fdFreshOffered(false, true));                                  // STANDALONE with the web page on home Wi-Fi
  CHECK(fdFreshOffered(true, true));                                   // fleet mode
  CHECK(!fdFreshOffered(true, false));                                 // ESP-NOW mode
}

// After START FRESH on a FluxDrive the next fling of that game to it is the original image, not the cable save: a mark per
// FluxDrive tag and image, kept on the card, until that FluxDrive has an own save again. Several targets (as Hivemind)
// still get the staged file; an own save always wins.
static void test_fd_after_start_fresh_the_original_is_sent() {
  const char* own = "/ADF/G.sav.80FE.adf";
  const char* cable = "/ADF/G.sav.adf";
  const char* image = "/ADF/G.adf";
  CHECK(std::strcmp(fdFileToSend(1, true, true, "80FE", false, own, cable, true, image), image) == 0);    // marked: the image
  CHECK(std::strcmp(fdFileToSend(1, true, true, "80FE", false, own, cable, false, image), cable) == 0);   // not marked: as before
  CHECK(std::strcmp(fdFileToSend(1, true, true, "80FE", false, own, cable), cable) == 0);
  CHECK(std::strcmp(fdFileToSend(1, true, true, "80FE", true, own, cable, true, image), own) == 0);       // an own save wins
  CHECK(std::strcmp(fdFileToSend(1, true, true, "80FE", false, own, image, true, image), image) == 0);    // no cable save
  CHECK(std::strcmp(fdFileToSend(2, true, true, "80FE", false, own, cable, true, image), cable) == 0);    // several targets
  CHECK(std::strcmp(fdFileToSend(1, false, true, "80FE", false, own, cable, true, image), cable) == 0);   // a Webby
  CHECK(std::strcmp(fdFileToSend(1, true, false, "80FE", false, own, cable, true, image), cable) == 0);   // SAVES=OFF
  CHECK(std::strcmp(fdFileToSend(1, true, true, "", false, own, cable, true, image), cable) == 0);        // no tag
  CHECK(std::strcmp(fdFileToSend(1, true, true, "80FE", false, own, cable, true, ""), cable) == 0);       // no image name
  CHECK(std::strcmp(fdFileToSend(1, true, true, "80FE", false, own, cable, true, nullptr), cable) == 0);
}

// The marks themselves: one per tag and image (an image path compared whole, case does not count), kept in a small table
// that survives a reboot as lines of a file. START FRESH adds one; the first fetch that writes the own save (or a fling that
// sends one) drops it; a full table forgets the oldest. With the mark gone and the own save there, the own save is sent.
static void test_fd_fresh_marks_are_added_found_and_dropped() {
  FdFreshSet s; fdFreshClearAll(s);
  CHECK(fdFreshFind(s, "80FE", "/ADF/G.adf") < 0);
  CHECK(fdFreshAdd(s, "80FE", "/ADF/G.adf"));
  CHECK(fdFreshFind(s, "80FE", "/ADF/G.adf") >= 0);
  CHECK(fdFreshFind(s, "80FE", "/adf/g.ADF") >= 0);                    // case does not count in a path
  CHECK(fdFreshFind(s, "00FF", "/ADF/G.adf") < 0);                     // another FluxDrive
  CHECK(fdFreshFind(s, "80FE", "/ADF/H.adf") < 0);                     // another game
  CHECK(fdFreshFind(s, "80FE", "/ADF/G.adf.x") < 0);
  CHECK(fdFreshFind(s, nullptr, "/ADF/G.adf") < 0 && fdFreshFind(s, "80FE", nullptr) < 0);
  CHECK(fdFreshAdd(s, "80FE", "/ADF/G.adf") && s.n == 1);              // twice: still one
  CHECK(!fdFreshAdd(s, "80fe", "/ADF/H.adf") && s.n == 1);             // not a tag
  CHECK(!fdFreshAdd(s, "80FE", "ADF/H.adf") && s.n == 1);              // not a path from the root
  CHECK(!fdFreshAdd(s, nullptr, "/ADF/H.adf") && !fdFreshAdd(s, "80FE", nullptr) && s.n == 1);
  char p160[200]; std::memset(p160, 'x', 160); p160[0] = '/'; p160[160] = 0;
  CHECK(!fdFreshAdd(s, "80FE", p160) && s.n == 1);                     // does not fit whole: not kept (a cut path names another)
  CHECK(fdFreshAdd(s, "00FF", "/ADF/G.adf") && s.n == 2);
  // the first fetch that writes Game.sav.80FE.adf: the mark goes, and the own save is what goes out from then on
  CHECK(fdFreshDrop(s, "80FE", "/adf/G.ADF") && s.n == 1);
  CHECK(fdFreshFind(s, "80FE", "/ADF/G.adf") < 0 && fdFreshFind(s, "00FF", "/ADF/G.adf") >= 0);
  CHECK(std::strcmp(fdFileToSend(1, true, true, "80FE", true, "/ADF/G.sav.80FE.adf", "/ADF/G.sav.adf",
                                 fdFreshFind(s, "80FE", "/ADF/G.adf") >= 0, "/ADF/G.adf"), "/ADF/G.sav.80FE.adf") == 0);
  CHECK(!fdFreshDrop(s, "80FE", "/ADF/G.adf") && s.n == 1);            // nothing to drop
  fdFreshClearAll(s);
  CHECK(s.n == 0);
  char path[40];
  for (int i = 0; i < FD_FRESH_MAX + 2; i++) { std::snprintf(path, sizeof path, "/ADF/G%02d.adf", i); CHECK(fdFreshAdd(s, "80FE", path)); }
  CHECK(s.n == FD_FRESH_MAX);
  CHECK(fdFreshFind(s, "80FE", "/ADF/G00.adf") < 0 && fdFreshFind(s, "80FE", "/ADF/G01.adf") < 0);   // the oldest forgotten
  std::snprintf(path, sizeof path, "/ADF/G%02d.adf", FD_FRESH_MAX + 1);
  CHECK(fdFreshFind(s, "80FE", path) >= 0 && fdFreshFind(s, "80FE", "/ADF/G02.adf") >= 0);
}

static void test_fd_fresh_mark_lines_round_trip_and_refuse_garbage() {
  FdFreshMark m; std::memset(&m, 0, sizeof m);
  std::strcpy(m.tag, "80FE"); std::strcpy(m.image, "/ADF/Lemmings (1991)/Lemmings.adf");
  char line[300];
  const int n = fdFreshFormat(m, line, sizeof line);
  CHECK(n > 0 && (size_t)n == std::strlen(line));
  CHECK(std::strcmp(line, "fresh\t80FE\t/ADF/Lemmings (1991)/Lemmings.adf") == 0);
  FdFreshMark b; std::memset(&b, 0x5A, sizeof b);
  CHECK(fdFreshParse(line, b) && std::strcmp(b.tag, m.tag) == 0 && std::strcmp(b.image, m.image) == 0);
  char lf[320]; std::snprintf(lf, sizeof lf, "%s\r\n", line);
  CHECK(fdFreshParse(lf, b) && std::strcmp(b.image, m.image) == 0);    // a newline (and a CR) at its end do not count
  const FdFreshMark keep = b;
  char ctl[40]; std::snprintf(ctl, sizeof ctl, "fresh\t80FE\t/ADF/G%c.adf", 1);
  char p160[200]; std::memset(p160, 'x', sizeof p160); std::memcpy(p160, "fresh\t80FE\t/", 12); p160[12 + 159] = 0;   // a 160-char path
  const char* bad[] = { "", "fresh", "fresh\t80FE", "fresh\t80FE\t", "fresh\t80fe\t/ADF/G.adf", "fresh\t80FE0\t/ADF/G.adf",
                        "fresh\t80FE\tADF/G.adf", "fresh\t80FE\t/ADF/G.adf\textra", "fresh 80FE /ADF/G.adf", ctl, p160,
                        "Fresh\t80FE\t/ADF/G.adf", "A1B2C3D4E5F6\t42\t80FE\t/ADF/G.adf\t/ADF/G.adf" };   // a save record is not a mark
  for (const char* l : bad) CHECK(!fdFreshParse(l, b) && std::strcmp(b.image, keep.image) == 0 && std::strcmp(b.tag, keep.tag) == 0);
  CHECK(!fdFreshParse(nullptr, b));
  FdFreshMark z; std::memset(&z, 0, sizeof z);
  CHECK(fdFreshFormat(z, line, sizeof line) == -1 && line[0] == 0);    // not a mark: nothing written
  CHECK(fdFreshFormat(m, line, 10) == -1);                             // does not fit whole
  CHECK(fdFreshFormat(m, nullptr, 10) == -1);
}

// ---- The commit window (fd3): FluxDrive says when the disk in it is in flash (its beacon's "stored") ------------------

// "stored" is false only when the beacon says so. A beacon without it - a Webby, a FluxDrive older than writing 10 - counts
// as stored: the panel knows of no commit window there.
static void test_fd_stored_field_missing_counts_as_stored() {
  CHECK(!fdStoredField("false"));
  CHECK(fdStoredField("true"));
  CHECK(fdStoredField(""));                                            // not in the beacon (pfJf gives "")
  CHECK(fdStoredField(nullptr));
  CHECK(fdStoredField("False") && fdStoredField("falsey") && fdStoredField("0"));   // only FluxDrive's own word means "not yet"
}

// "STORING..." (NOW PLAYING, the fleet row) and "do not switch off yet" (the status bar): a FluxDrive holding a disk that is
// not in flash yet. An empty FluxDrive says stored false as well and never shows it; no other board ever does.
static void test_fd_storing_shows_only_with_a_disk_not_yet_stored() {
  CHECK(fdShowStoring(true, true, false));
  CHECK(!fdShowStoring(true, true, true));                             // stored: nothing to wait for
  CHECK(!fdShowStoring(true, false, false));                           // empty (an eject inside the window)
  CHECK(!fdShowStoring(true, false, true));
  CHECK(!fdShowStoring(false, true, false));                           // a Webby
}

// The fleet manager repaints a FluxDrive row when its STORING... comes or goes.
static void test_the_row_signature_follows_storing() {
  char a[120], b[120];
  CHECK(fdRowSig("fluxdrive", true, true, "Lemmings", a, sizeof(a), false) > 0);
  CHECK(fdRowSig("fluxdrive", true, true, "Lemmings", b, sizeof(b), false, true) > 0 && std::strcmp(a, b) != 0);   // storing
  CHECK(fdRowSig("fluxdrive", true, true, "Lemmings", b, sizeof(b), false, false) > 0 && std::strcmp(a, b) == 0);  // default: not
  CHECK(fdRowSig("fluxdrive", true, true, "Lemmings", a, sizeof(a), true, true) > 0 &&
        fdRowSig("fluxdrive", true, true, "Lemmings", b, sizeof(b), true, false) > 0 && std::strcmp(a, b) != 0);   // WP on as well
  CHECK(fdRowSig("fluxdrive", true, true, "Lemmings", a, sizeof(a), true, true) > 0 &&
        fdRowSig("fluxdrive", true, true, "Lemmings", b, sizeof(b), false, true) > 0 && std::strcmp(a, b) != 0);   // WP still counts
  CHECK(fdRowSig("supermini", true, true, "Lemmings", a, sizeof(a), false, true) == 0 && a[0] == 0);
}

// Until the disk a fling sent is stored, the panel keeps the record that fling replaced (prev). What a beacon of that
// FluxDrive means for it (cur: its record now; the beacon's load_id, loaded and stored):
//  Restore  - a disk in it with prev's uid: switched off before the new disk was stored, FluxDrive went back to prev;
//  DropPrev - a disk in it with cur's uid, and stored: prev can never come back;
//  Keep     - anything else (not stored yet, empty, another disk, no uid, nothing kept).
static const FdSaveRecord kCur = fdRec("A1B2C3D4E5F6", 43, "/ADF/H.adf", "/ADF/H.adf", "80FE");
static const FdSaveRecord kPrev = fdRec("A1B2C3D4E5F6", 42, "/ADF/G.sav.80FE.adf", "/ADF/G.adf", "80FE");

static void test_fd_revert_check_restore_when_the_beacon_reports_prev() {
  CHECK(fdRevertCheck(&kCur, &kPrev, 42, true, true) == FdRevert::Restore);    // loaded from flash at its boot: stored
  CHECK(fdRevertCheck(&kCur, &kPrev, 42, true, false) == FdRevert::Restore);   // whatever it says of stored
  CHECK(fdRevertCheck(nullptr, &kPrev, 42, true, true) == FdRevert::Restore);  // the new record gone meanwhile: still back
  FdSaveRecord hi = kPrev; hi.uid = 4294967295u;
  CHECK(fdRevertCheck(&kCur, &hi, 4294967295u, true, true) == FdRevert::Restore);   // the whole 32 bits
}

static void test_fd_revert_check_drop_prev_when_the_new_uid_is_stored() {
  CHECK(fdRevertCheck(&kCur, &kPrev, 43, true, true) == FdRevert::DropPrev);
  FdSaveRecord hi = kCur; hi.uid = 4294967295u;
  CHECK(fdRevertCheck(&hi, &kPrev, 4294967295u, true, true) == FdRevert::DropPrev);
}

static void test_fd_revert_check_keep_while_the_new_uid_is_not_stored() {
  CHECK(fdRevertCheck(&kCur, &kPrev, 43, true, false) == FdRevert::Keep);     // its commit runs (8-82 s), or gave up
}

static void test_fd_revert_check_keep_without_prev() {
  CHECK(fdRevertCheck(&kCur, nullptr, 43, true, true) == FdRevert::Keep);
  CHECK(fdRevertCheck(&kCur, nullptr, 42, true, true) == FdRevert::Keep);
  CHECK(fdRevertCheck(nullptr, nullptr, 42, true, true) == FdRevert::Keep);
  FdSaveRecord z = kPrev; z.uid = 0;                                   // a uid 0 matches nothing, not even "none"
  CHECK(fdRevertCheck(&kCur, &z, 0, true, true) == FdRevert::Keep);
  CHECK(fdRevertCheck(&kCur, &z, 43, true, true) == FdRevert::Keep);
}

// An eject inside the window says stored false because the drive is empty, and FluxDrive keeps an ejected disk's uid:
// neither drops prev (nor deletes a START FRESH backup) nor brings it back.
static void test_fd_revert_check_keep_when_not_loaded() {
  CHECK(fdRevertCheck(&kCur, &kPrev, 43, false, false) == FdRevert::Keep);
  CHECK(fdRevertCheck(&kCur, &kPrev, 43, false, true) == FdRevert::Keep);
  CHECK(fdRevertCheck(&kCur, &kPrev, 42, false, true) == FdRevert::Keep);
  CHECK(fdRevertCheck(&kCur, &kPrev, 42, false, false) == FdRevert::Keep);
  CHECK(fdRevertCheck(&kCur, &kPrev, 0, false, false) == FdRevert::Keep);
}

static void test_fd_revert_check_keep_for_anything_else() {
  CHECK(fdRevertCheck(&kCur, &kPrev, 44, true, true) == FdRevert::Keep);     // a third disk (a PC push, another panel)
  CHECK(fdRevertCheck(&kCur, &kPrev, 44, true, false) == FdRevert::Keep);
  CHECK(fdRevertCheck(&kCur, &kPrev, 0, true, true) == FdRevert::Keep);      // no uid in the beacon
  CHECK(fdRevertCheck(nullptr, &kPrev, 0, true, true) == FdRevert::Keep);    // ... not even with no new record
  CHECK(fdRevertCheck(nullptr, &kPrev, 43, true, true) == FdRevert::Keep);   // no new record: nothing says which disk is new
  FdSaveRecord c32 = kCur, p32 = kPrev; c32.uid = 0x00010043u; p32.uid = 0x00010042u;   // uids are 32 bits, all of them count
  CHECK(fdRevertCheck(&c32, &p32, 0x00020042u, true, true) == FdRevert::Keep);
  CHECK(fdRevertCheck(&c32, &p32, 0x00020043u, true, true) == FdRevert::Keep);
  CHECK(fdRevertCheck(&c32, &p32, 0x00010042u, true, true) == FdRevert::Restore);
  FdSaveRecord other = kCur; std::strcpy(other.id, "0123456789AB");          // records of two FluxDrives are not a pair
  CHECK(fdRevertCheck(&other, &kPrev, 42, true, true) == FdRevert::Keep);
  CHECK(fdRevertCheck(&other, &kPrev, 43, true, true) == FdRevert::Keep);
  const FdSaveRecord same = kPrev;                                     // the "new" record is the kept one: no revert
  CHECK(fdRevertCheck(&same, &kPrev, 42, true, false) == FdRevert::Keep);
  CHECK(fdRevertCheck(&same, &kPrev, 42, true, true) == FdRevert::DropPrev);
}

// fix round 1: per FluxDrive a short list (FD_PREV_MAX) of the records flings replaced since its disk was last seen stored.
// B can be stored without the panel seeing it (ejected while its commit finished, a dropped beacon, UDP loss), so a power
// cut in C's window can bring back A or B: both are kept. Each fling appends the record it replaces; a full list drops the
// oldest. A beacon with a disk whose uid is any kept one restores that one; the current disk stored clears the list.
static const FdSaveRecord kA = fdRec("A1B2C3D4E5F6", 41, "/ADF/F.sav.80FE.adf", "/ADF/F.adf", "80FE");
static const FdSaveRecord kB = fdRec("A1B2C3D4E5F6", 42, "/ADF/G.sav.80FE.adf", "/ADF/G.adf", "80FE");
static const FdSaveRecord kC = fdRec("A1B2C3D4E5F6", 43, "/ADF/H.adf", "/ADF/H.adf", "80FE");

static void test_fd_prev_list_a_b_c_reverts_to_a_or_b_or_none() {
  FdPrevList l; fdPrevClear(l);
  FdPrevOut o;
  CHECK(fdPrevAtFling(l, nullptr, kA, false, &o) == FD_PREV_NONE && l.n == 0);   // the first fling to it: nothing replaced
  CHECK(fdPrevAtFling(l, &kA, kB, false, &o) == FD_PREV_KEPT && l.n == 1 && l.r[0].uid == 41 && !o.evicted);
  CHECK(fdPrevAtFling(l, &kB, kC, false, &o) == FD_PREV_KEPT && l.n == 2 && l.r[0].uid == 41 && l.r[1].uid == 42);
  int w = -1;
  CHECK(fdRevertList(&kC, l, 41, true, true, &w) == FdRevert::Restore && w == 0);    // back to A
  CHECK(fdRevertList(&kC, l, 42, true, true, &w) == FdRevert::Restore && w == 1);    // back to B (stored, never seen so)
  CHECK(fdRevertList(&kC, l, 42, true, false, &w) == FdRevert::Restore && w == 1);
  CHECK(fdRevertList(&kC, l, 43, true, true, &w) == FdRevert::DropPrev && w == -1);  // C stored: the whole list goes
  CHECK(fdRevertList(&kC, l, 43, true, false, &w) == FdRevert::Keep && w == -1);     // none: C's commit still runs
  CHECK(fdRevertList(&kC, l, 42, false, true, &w) == FdRevert::Keep && w == -1);     // none: empty drive (B's uid kept)
  CHECK(fdRevertList(&kC, l, 44, true, true, &w) == FdRevert::Keep);                 // none: a third disk
  CHECK(fdRevertList(&kC, l, 0, true, true, &w) == FdRevert::Keep);
  CHECK(fdRevertList(nullptr, l, 41, true, true, &w) == FdRevert::Restore && w == 0);   // its record gone meanwhile
  CHECK(fdRevertList(nullptr, l, 43, true, true, &w) == FdRevert::Keep);
  FdPrevList e; fdPrevClear(e);
  CHECK(fdRevertList(&kC, e, 43, true, true, &w) == FdRevert::Keep && w == -1);      // nothing kept: nothing to do
  FdSaveRecord other = kC; std::strcpy(other.id, "0123456789AB");
  CHECK(fdRevertList(&other, l, 41, true, true, &w) == FdRevert::Keep);              // another FluxDrive's record
}

static void test_fd_prev_list_drops_the_oldest_when_full() {
  FdPrevList l; fdPrevClear(l);
  FdPrevOut o;
  FdSaveRecord r[6];
  for (int i = 0; i < 6; i++) {
    char orig[40], own[40]; std::snprintf(orig, sizeof orig, "/ADF/G%d.adf", i); std::snprintf(own, sizeof own, "/ADF/G%d.sav.80FE.adf", i);
    r[i] = fdRec("A1B2C3D4E5F6", 100u + (uint32_t)i, own, orig, "80FE");
  }
  for (int i = 1; i <= FD_PREV_MAX; i++) CHECK(fdPrevAtFling(l, &r[i - 1], r[i], false, &o) == FD_PREV_KEPT && !o.evicted);
  CHECK(l.n == FD_PREV_MAX);
  CHECK(fdPrevAtFling(l, &r[FD_PREV_MAX], r[FD_PREV_MAX + 1], false, &o) == FD_PREV_KEPT && o.evicted && o.ev.uid == 100 &&
        l.n == FD_PREV_MAX && l.r[0].uid == 101 && l.r[FD_PREV_MAX - 1].uid == 100u + FD_PREV_MAX);
  int w = -1;
  CHECK(fdRevertList(&r[FD_PREV_MAX + 1], l, 100, true, true, &w) == FdRevert::Keep);   // the dropped one is gone
  CHECK(fdRevertList(&r[FD_PREV_MAX + 1], l, 101, true, true, &w) == FdRevert::Restore && w == 0);
}

// fix round 1 Fix 3: the same game flung again (same image, same FluxDrive, so the same own save) keeps no replaced record:
// the new disk's fetches patch the own save in place during the window, and the old record's later sectors would land on
// top of them (a mixed save). An older kept record of that game goes too, for the same reason. START FRESH's fling of the
// original keeps it (keep_same_game): its own save is set aside and comes back with an undo, so nothing is mixed.
static void test_fd_prev_list_same_game_keeps_nothing() {
  FdPrevList l; fdPrevClear(l);
  FdPrevOut o;
  FdSaveRecord b2 = kB; b2.uid = 52;                                   // G again, a new disk
  CHECK(fdPrevAtFling(l, &kB, b2, false, &o) == FD_PREV_SAME_GAME && l.n == 0);
  FdSaveRecord b3 = kB; b3.uid = 53; std::strcpy(b3.orig, "/adf/g.ADF"); // case does not count in a path
  CHECK(fdPrevAtFling(l, &kB, b3, false, &o) == FD_PREV_SAME_GAME && l.n == 0);
  CHECK(fdPrevAtFling(l, &kB, b2, true, &o) == FD_PREV_KEPT && l.n == 1);  // START FRESH: kept, its undo puts the save back
  fdPrevClear(l);
  // G, then H, then G again: G's record goes with the second G fling; H's is kept
  CHECK(fdPrevAtFling(l, &kB, kC, false, &o) == FD_PREV_KEPT && l.n == 1);
  CHECK(fdPrevAtFling(l, &kC, b2, false, &o) == FD_PREV_KEPT && o.same_dropped == 1 && l.n == 1 && l.r[0].uid == 43);
  FdSaveRecord t = kB; std::strcpy(t.tag, "00FF");                     // another save tag: another own save
  fdPrevClear(l);
  CHECK(fdPrevAtFling(l, &t, b2, false, &o) == FD_PREV_KEPT && o.same_dropped == 0);
  CHECK(fdSameGame(kB, b2) && fdSameGame(kB, b3) && !fdSameGame(kB, kC) && !fdSameGame(kB, t));
}

static void test_fd_prev_list_keeps_nothing_else() {
  FdPrevList l; fdPrevClear(l);
  FdPrevOut o;
  FdSaveRecord z = kB; z.uid = 0;
  CHECK(fdPrevAtFling(l, &z, kC, false, &o) == FD_PREV_NONE && l.n == 0);   // a record without a uid
  CHECK(fdPrevAtFling(l, &kC, kC, false, &o) == FD_PREV_NONE && l.n == 0);  // the new disk itself
  FdSaveRecord other = kB; std::strcpy(other.id, "0123456789AB");
  CHECK(fdPrevAtFling(l, &other, kC, false, &o) == FD_PREV_NONE && l.n == 0);   // another FluxDrive's record
  CHECK(fdPrevAtFling(l, &kA, kC, false, nullptr) == FD_PREV_KEPT && l.n == 1);   // no out: still kept
  CHECK(fdPrevAtFling(l, &kA, kC, false, &o) == FD_PREV_KEPT && l.n == 1);   // the same record twice: kept once
}

// START FRESH's backup has a name of its own, with the fresh disk's uid in it, so two START FRESHes never share a file and
// fdPersistLan's rebuild backup (Game.sav.<tag>.bak.adf) never touches it; an undo puts a save written in the window aside
// under a name of its own too.
static void test_fd_save_aside_names() {
  char out[160];
  CHECK(fdSaveAside("/ADF/G.sav.80FE.adf", "fresh", 0x2Bu, out, sizeof out) &&
        std::strcmp(out, "/ADF/G.sav.80FE.fresh-0000002B.adf") == 0);
  CHECK(fdSaveAside("/ADF/G.sav.80FE.adf", "undone", 0xDEADBEEFu, out, sizeof out) &&
        std::strcmp(out, "/ADF/G.sav.80FE.undone-DEADBEEF.adf") == 0);
  CHECK(fdSaveAside("/ADF/x.y/G", "fresh", 1, out, sizeof out) && std::strcmp(out, "/ADF/x.y/G.fresh-00000001") == 0);   // no extension
  char p150[151]; fdLongPath(p150, 150);
  CHECK(!fdSaveAside(p150, "fresh", 1, out, sizeof out) && out[0] == 0);   // does not fit whole: nothing (a cut name is another file)
  CHECK(!fdSaveAside(nullptr, "fresh", 1, out, sizeof out) && !fdSaveAside("/ADF/G.adf", "fresh", 1, nullptr, 10));
}

// START FRESH on a FluxDrive that gets the original now: the own save is renamed to its .bak and this entry stays in RAM
// (FluxDrive id, the fresh disk's uid, the uid of the disk it replaced, the own save, the .bak, the image, the save tag).
static void test_fd_fresh_pending_is_made_whole_or_not_at_all() {
  const char* own = "/ADF/G.sav.80FE.adf";
  const char* bak = "/ADF/G.sav.80FE.fresh-0000002B.adf";
  const char* img = "/ADF/G.adf";
  FdFreshPending p; std::memset(&p, 0x5A, sizeof p);
  CHECK(fdFreshPendingSet(p, "A1B2C3D4E5F6", 43, 42, own, bak, img, "80FE"));
  CHECK(std::strcmp(p.id, "A1B2C3D4E5F6") == 0 && p.fresh_uid == 43 && p.prior_uid == 42 && std::strcmp(p.own, own) == 0 &&
        std::strcmp(p.bak, bak) == 0 && std::strcmp(p.image, img) == 0 && std::strcmp(p.tag, "80FE") == 0 && !p.undo_failed);
  const FdFreshPending keep = p;
  char p159[160]; fdLongPath(p159, 159);
  char p160[161]; fdLongPath(p160, 160);
  CHECK(fdFreshPendingSet(p, "A1B2C3D4E5F6", 43, 42, p159, bak, img, "80FE") && std::strcmp(p.own, p159) == 0);
  p = keep;
  struct Bad { const char *id; uint32_t fresh, prior; const char *own, *bak, *img, *tag; } bad[] = {
    { "A1B2C3D4E5F6", 0, 42, own, bak, img, "80FE" },                  // no fresh uid (no uid in the ack)
    { "A1B2C3D4E5F6", 43, 0, own, bak, img, "80FE" },                  // no prior uid
    { "A1B2C3D4E5F6", 42, 42, own, bak, img, "80FE" },                 // the same disk
    { "", 43, 42, own, bak, img, "80FE" },
    { "a1b2c3d4e5f6", 43, 42, own, bak, img, "80FE" },
    { "A1B2C3D4E5F6A", 43, 42, own, bak, img, "80FE" },
    { nullptr, 43, 42, own, bak, img, "80FE" },
    { "A1B2C3D4E5F6", 43, 42, "ADF/G.sav.80FE.adf", bak, img, "80FE" },
    { "A1B2C3D4E5F6", 43, 42, own, "", img, "80FE" },
    { "A1B2C3D4E5F6", 43, 42, own, bak, nullptr, "80FE" },
    { "A1B2C3D4E5F6", 43, 42, own, bak, "/ADF/G\t.adf", "80FE" },
    { "A1B2C3D4E5F6", 43, 42, p160, bak, img, "80FE" },                // does not fit whole: a cut path names another file
    { "A1B2C3D4E5F6", 43, 42, own, p160, img, "80FE" },
    { "A1B2C3D4E5F6", 43, 42, own, bak, p160, "80FE" },
    { "A1B2C3D4E5F6", 43, 42, own, bak, img, "80fe" },
    { "A1B2C3D4E5F6", 43, 42, own, bak, img, "80FE0" },
    { "A1B2C3D4E5F6", 43, 42, own, bak, img, nullptr },
  };
  for (const Bad& x : bad)
    CHECK(!fdFreshPendingSet(p, x.id, x.fresh, x.prior, x.own, x.bak, x.img, x.tag) && std::memcmp(&p, &keep, sizeof p) == 0);
}

// What a beacon of that FluxDrive means for a pending START FRESH (cur: the uid of its record now, the disk last flung to
// it): the fresh disk - or the current one, flung after it - is in it and stored: done, the backup goes (DropPrev; no older
// disk can come back then); the disk START FRESH replaced is in it again: undo (Restore); else Keep. An eject inside the
// window keeps it, and so does an undo whose renames failed (undo_failed: the backup stays where it is).
static void test_fd_fresh_pending_check() {
  FdFreshPending p; std::memset(&p, 0, sizeof p);
  CHECK(fdFreshPendingSet(p, "A1B2C3D4E5F6", 43, 42, "/ADF/G.sav.80FE.adf", "/ADF/G.sav.80FE.fresh-0000002B.adf", "/ADF/G.adf", "80FE"));
  CHECK(fdFreshPendingCheck(&p, 43, 43, true, true) == FdRevert::DropPrev);
  CHECK(fdFreshPendingCheck(&p, 43, 43, true, false) == FdRevert::Keep);
  CHECK(fdFreshPendingCheck(&p, 43, 42, true, true) == FdRevert::Restore);
  CHECK(fdFreshPendingCheck(&p, 43, 42, true, false) == FdRevert::Restore);
  CHECK(fdFreshPendingCheck(&p, 43, 43, false, false) == FdRevert::Keep);   // ejected inside the window: the backup stays
  CHECK(fdFreshPendingCheck(&p, 43, 42, false, true) == FdRevert::Keep);
  CHECK(fdFreshPendingCheck(&p, 43, 44, true, true) == FdRevert::Keep);     // a third disk (not flung by this panel)
  CHECK(fdFreshPendingCheck(&p, 43, 0, true, true) == FdRevert::Keep);
  CHECK(fdFreshPendingCheck(nullptr, 43, 43, true, true) == FdRevert::Keep);
  // another disk flung after the original: once it is stored neither the original nor the replaced disk can come back
  CHECK(fdFreshPendingCheck(&p, 44, 44, true, true) == FdRevert::DropPrev);
  CHECK(fdFreshPendingCheck(&p, 44, 44, true, false) == FdRevert::Keep);
  CHECK(fdFreshPendingCheck(&p, 44, 43, true, true) == FdRevert::DropPrev);  // the original came back stored: done as well
  CHECK(fdFreshPendingCheck(&p, 44, 42, true, true) == FdRevert::Restore);
  CHECK(fdFreshPendingCheck(&p, 0, 43, true, true) == FdRevert::DropPrev);
  CHECK(fdFreshPendingCheck(&p, 0, 0, true, true) == FdRevert::Keep);
  p.undo_failed = true;                                                      // its undo failed: frozen, the backup stays
  CHECK(fdFreshPendingCheck(&p, 43, 42, true, true) == FdRevert::Keep);
  CHECK(fdFreshPendingCheck(&p, 43, 43, true, true) == FdRevert::Keep);
}

// fix round 2: START FRESH on G with a resend (the original is disk 43, it replaced 42) and then on H without one in the same
// window (fresh = the disk in it now, 43 as well; H's disk 41 is a kept record). Both share the FluxDrive and the fresh uid,
// never the backup: two entries, and whichever disk comes back undoes only its own START FRESH with its own backup.
static void test_fd_two_games_started_fresh_in_one_window() {
  const char* id = "A1B2C3D4E5F6";
  FdFreshPending t[4]; std::memset(t, 0, sizeof t); int n = 0;
  FdFreshPending g, h, ev; std::memset(&g, 0, sizeof g); std::memset(&h, 0, sizeof h);
  CHECK(fdFreshPendingSet(g, id, 43, 42, "/ADF/G.sav.80FE.adf", "/ADF/G.sav.80FE.fresh-0000002B.adf", "/ADF/G.adf", "80FE"));
  CHECK(fdFreshPendingSet(h, id, 43, 41, "/ADF/H.sav.80FE.adf", "/ADF/H.sav.80FE.fresh-0000002B.adf", "/ADF/H.adf", "80FE"));
  CHECK(!fdPendSame(g, h));
  CHECK(fdPendPut(t, n, 4, g, &ev) == FD_PEND_ADDED && n == 1);
  CHECK(fdPendPut(t, n, 4, h, &ev) == FD_PEND_ADDED && n == 2);       // H does not replace G
  auto undoes = [&](uint32_t cur, uint32_t beacon) -> const FdFreshPending* {   // the one entry a beacon undoes, else none
    const FdFreshPending* u = nullptr; int k = 0;
    for (int i = 0; i < n; i++) if (fdFreshPendingCheck(&t[i], cur, beacon, true, true) == FdRevert::Restore) { u = &t[i]; k++; }
    return k == 1 ? u : nullptr;
  };
  const FdFreshPending* u = undoes(43, 42);                            // G's disk back: G's own backup, G's own save
  CHECK(u != nullptr && std::strcmp(u->bak, "/ADF/G.sav.80FE.fresh-0000002B.adf") == 0 && std::strcmp(u->own, "/ADF/G.sav.80FE.adf") == 0);
  u = undoes(43, 41);                                                  // H's disk back: H's
  CHECK(u != nullptr && std::strcmp(u->bak, "/ADF/H.sav.80FE.fresh-0000002B.adf") == 0 && std::strcmp(u->own, "/ADF/H.sav.80FE.adf") == 0);
  CHECK(undoes(43, 43) == nullptr && undoes(43, 44) == nullptr);
  CHECK(fdFreshPendingCheck(&t[1], 42, 42, true, true) == FdRevert::DropPrev);   // G's disk back and stored: H's stands
  CHECK(fdFreshPendingCheck(&t[0], 41, 41, true, true) == FdRevert::DropPrev);   // H's disk back and stored: G's stands
  CHECK(fdFreshPendingCheck(&t[0], 43, 43, true, true) == FdRevert::DropPrev &&
        fdFreshPendingCheck(&t[1], 43, 43, true, true) == FdRevert::DropPrev);   // the original stored: both done
  // the same START FRESH again (FluxDrive, fresh disk and backup) replaces its entry; anything else is another one
  FdFreshPending g2 = g; g2.prior_uid = 40;
  CHECK(fdPendSame(g, g2) && fdPendPut(t, n, 4, g2, &ev) == FD_PEND_REPLACED && n == 2 && t[0].prior_uid == 40 && t[1].prior_uid == 41);
  FdFreshPending g3 = g; std::strcpy(g3.bak, "/adf/g.SAV.80fe.FRESH-0000002b.ADF");   // a FAT name: case does not count
  CHECK(fdPendSame(g, g3));
  FdFreshPending o = g; std::strcpy(o.id, "0123456789AB");
  CHECK(!fdPendSame(g, o));                                            // another FluxDrive
  o = g; o.fresh_uid = 44;
  CHECK(!fdPendSame(g, o));                                            // another fresh disk
}

// A full pending table forgets its oldest entry and says which (the caller logs it: that backup stays on the card).
static void test_fd_pending_table_full_evicts_the_oldest() {
  FdFreshPending t[3]; std::memset(t, 0, sizeof t); int n = 0;
  FdFreshPending p, ev; std::memset(&p, 0, sizeof p); std::memset(&ev, 0, sizeof ev);
  for (uint32_t i = 0; i < 3; i++) {
    char bak[64]; std::snprintf(bak, sizeof bak, "/ADF/G%u.sav.80FE.fresh-%08X.adf", (unsigned)i, (unsigned)(50 + i));
    CHECK(fdFreshPendingSet(p, "A1B2C3D4E5F6", 50 + i, 40, "/ADF/G.sav.80FE.adf", bak, "/ADF/G.adf", "80FE"));
    CHECK(fdPendPut(t, n, 3, p, &ev) == FD_PEND_ADDED);
  }
  CHECK(fdFreshPendingSet(p, "A1B2C3D4E5F6", 53, 40, "/ADF/G.sav.80FE.adf", "/ADF/G3.sav.80FE.fresh-00000035.adf", "/ADF/G.adf", "80FE"));
  CHECK(fdPendPut(t, n, 3, p, &ev) == FD_PEND_EVICTED && ev.fresh_uid == 50 && n == 3 && t[0].fresh_uid == 51 && t[2].fresh_uid == 53);
  p.fresh_uid = 54; std::strcpy(p.bak, "/ADF/G4.sav.80FE.fresh-00000036.adf");
  CHECK(fdPendPut(t, n, 3, p, nullptr) == FD_PEND_EVICTED && n == 3 && t[0].fresh_uid == 52);   // without an out: still evicts
}

// The START FRESH confirm says "reset the Amiga afterwards" for a DOS disk: AmigaDOS takes two disks with the same name
// and date as one volume. A DOS disk's boot block starts "DOS" and a flags byte 0-7.
static void test_fd_dos_boot_block() {
  const uint8_t ofs[4] = { 'D', 'O', 'S', 0 }, ffs[4] = { 'D', 'O', 'S', 1 }, dc[4] = { 'D', 'O', 'S', 7 };
  const uint8_t f8[4] = { 'D', 'O', 'S', 8 }, ndos[4] = { 'N', 'D', 'O', 'S' }, kick[4] = { 'K', 'I', 'C', 'K' };
  const uint8_t low[4] = { 'd', 'o', 's', 0 }, dxs[4] = { 'D', 'X', 'S', 0 }, xos[4] = { 'X', 'O', 'S', 0 }, dox[4] = { 'D', 'O', 'X', 0 };
  CHECK(fdIsDosBoot(ofs, 4) && fdIsDosBoot(ffs, 4) && fdIsDosBoot(dc, 4));
  CHECK(!fdIsDosBoot(f8, 4) && !fdIsDosBoot(ndos, 4) && !fdIsDosBoot(kick, 4) && !fdIsDosBoot(low, 4));
  CHECK(!fdIsDosBoot(dxs, 4) && !fdIsDosBoot(xos, 4) && !fdIsDosBoot(dox, 4));
  CHECK(!fdIsDosBoot(ofs, 3));                                         // a short read
  CHECK(!fdIsDosBoot(nullptr, 4));
}

// ---- DF1 from the touchscreen (A600-lab2i-fd4): the wire frames, the split INSERT, the record key, the fetch, the bar ----

static bool frameIs(const uint8_t* f, size_t n, const uint8_t* want, size_t wn) { return n == wn && std::memcmp(f, want, n) == 0; }

// The new commands carry the drive as one byte after the escape; the old ones (drive 0) are sent byte for byte as before.
static void test_df1_frames() {
  uint8_t f[6];
  const uint8_t setd1[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x11, 0x01};
  CHECK(fdDriveFrame(FD_CMD_SET_DRIVE, 1, f) == 6 && frameIs(f, 6, setd1, 6));
  const uint8_t setd0[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x11, 0x00};
  CHECK(fdDriveFrame(FD_CMD_SET_DRIVE, 0, f) == 6 && frameIs(f, 6, setd0, 6));
  const uint8_t save0[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x01};
  CHECK(fdSaveFrame(0, f) == 5 && frameIs(f, 5, save0, 5));                 // DF0: GET_SAVE exactly as before
  const uint8_t save1[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x12, 0x01};
  CHECK(fdSaveFrame(1, f) == 6 && frameIs(f, 6, save1, 6));
  const uint8_t ej0[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x03};
  CHECK(fdEjectFrame(0, f) == 5 && frameIs(f, 5, ej0, 5));                  // DF0: EJECT exactly as before
  const uint8_t ej1[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x13, 0x01};
  CHECK(fdEjectFrame(1, f) == 6 && frameIs(f, 6, ej1, 6));
  CHECK(fdSaveFrame(7, f) == 5);                                            // anything but 1 is DF0
}

// SET_DRIVE must be acked before a push: without the ack the disk would land in DF0.
static void test_df1_set_drive_answer() {
  CHECK(fdSetDriveFail(0x01, false) == nullptr);
  CHECK(std::strcmp(fdSetDriveFail(0x00, false), "DF1 is off on the FluxDrive") == 0);
  CHECK(fdSetDriveFail(-1, true) != nullptr && std::strcmp(fdSetDriveFail(-1, true), fdBusyText()) == 0);
  CHECK(fdSetDriveFail(-1, false) != nullptr);                              // closed early, no ack: still not a go
  CHECK(fdSetDriveFail(0x05, false) != nullptr);
  CHECK(fdSetDriveFail(0x02, false) != nullptr);
}

// The split: a single FluxDrive that is the target and whose beacon says df1. Anything else stays one INSERT.
static void test_df1_split_decision() {
  CHECK(fdInsertSplit(true, true, true));
  CHECK(!fdInsertSplit(false, true, true));      // several peers (the fleet manager), or a dongle
  CHECK(!fdInsertSplit(true, false, true));      // the single FluxDrive is not the one the screen speaks for
  CHECK(!fdInsertSplit(true, true, false));      // an older FluxDrive, or DF1 off
  CHECK(!fdInsertSplit(false, false, false));
}

static void test_df1_split_halves_fill_the_button() {
  const FdRect ins = {4, 244, 142, 28};
  FdRect h[2];
  fdInsertHalves(ins, 4, h);
  CHECK(h[0].x == 4 && h[0].w == 69 && h[0].y == 244 && h[0].h == 28);
  CHECK(h[1].x == 4 + 69 + 4 && h[1].w == 142 - 69 - 4 && h[1].y == 244 && h[1].h == 28);
  CHECK(h[0].x + h[0].w + 4 == h[1].x && h[1].x + h[1].w == ins.x + ins.w);   // nothing lost, nothing past the button
  const FdRect strip = {234, 120, 66, 26};                                    // compact portrait, the narrowest
  fdInsertHalves(strip, 2, h);
  CHECK(h[0].w == 32 && h[1].x == 234 + 32 + 2 && h[1].w == 32 && h[1].x + h[1].w == 300);
  const FdRect odd = {0, 0, 81, 20};
  fdInsertHalves(odd, 3, h);
  CHECK(h[0].w == 39 && h[1].w == 39 && h[1].x + h[1].w == 81);
}

static void test_df1_insert_hit() {
  const FdRect ins = {4, 244, 142, 28};
  CHECK(fdInsertHit(ins, 4, false, 10, 250) == 0);              // one INSERT: DF0, wherever inside
  CHECK(fdInsertHit(ins, 4, false, 140, 270) == 0);
  CHECK(fdInsertHit(ins, 4, false, 3, 250) == -1);              // outside
  CHECK(fdInsertHit(ins, 4, false, 146, 250) == -1);
  CHECK(fdInsertHit(ins, 4, false, 10, 243) == -1);
  CHECK(fdInsertHit(ins, 4, false, 10, 272) == -1);
  CHECK(fdInsertHit(ins, 4, true, 4, 244) == 0);                // split: left half DF0, right half DF1
  CHECK(fdInsertHit(ins, 4, true, 72, 260) == 0);
  CHECK(fdInsertHit(ins, 4, true, 74, 260) == 0);               // the gap goes to the nearer half (the middle line)
  CHECK(fdInsertHit(ins, 4, true, 75, 260) == 1);
  CHECK(fdInsertHit(ins, 4, true, 77, 260) == 1);
  CHECK(fdInsertHit(ins, 4, true, 145, 271) == 1);
  CHECK(fdInsertHit(ins, 4, true, 146, 260) == -1);
  CHECK(fdInsertHit(ins, 4, true, 3, 260) == -1);
}

// A tap on a drive's half: eject when the selected disk is what the drive holds, otherwise load it (a clean swap).
static void test_df1_tap_decision() {
  CHECK(!fdInsertTapEjects(false, -1, 0));      // the game is not in that drive: load
  CHECK(!fdInsertTapEjects(false, 0, 0));
  CHECK(fdInsertTapEjects(true, -1, 0));        // in it, disk not known here: eject
  CHECK(fdInsertTapEjects(true, 1, 1));         // in it, that very disk: eject
  CHECK(!fdInsertTapEjects(true, 0, 1));        // another disk of the game selected: swap to it
}

static void test_df1_half_text() {
  const char *a = nullptr, *b = nullptr;
  fdHalfText(0, false, &a, &b); CHECK(std::strcmp(a, "DF0") == 0 && b == nullptr);
  fdHalfText(1, false, &a, &b); CHECK(std::strcmp(a, "DF1") == 0 && b == nullptr);
  fdHalfText(0, true, &a, &b);  CHECK(std::strcmp(a, "EJECT") == 0 && std::strcmp(b, "DF0") == 0);
  fdHalfText(1, true, &a, &b);  CHECK(std::strcmp(a, "EJECT") == 0 && std::strcmp(b, "DF1") == 0);
}

// The save record is keyed by FluxDrive and drive: "<12 hex>" for DF0 (the old key), "<12 hex>:1" for DF1.
static void test_df1_record_key() {
  char k[20];
  CHECK(fdRecKey("A1B2C3D4E5F6", 0, k, sizeof k) && std::strcmp(k, "A1B2C3D4E5F6") == 0);
  CHECK(fdRecKey("A1B2C3D4E5F6", 1, k, sizeof k) && std::strcmp(k, "A1B2C3D4E5F6:1") == 0);
  CHECK(!fdRecKey("A1B2C3D4E5F6", 1, k, 14) && k[0] == 0);                // does not fit whole
  CHECK(fdRecKey("A1B2C3D4E5F6", 1, k, 15));
  CHECK(!fdRecKey(nullptr, 0, k, sizeof k));
  CHECK(!fdRecKey("A1B2C3D4E5F6", 0, nullptr, 0));
  CHECK(fdRecKeyDrive("A1B2C3D4E5F6") == 0);
  CHECK(fdRecKeyDrive("A1B2C3D4E5F6:1") == 1);
  CHECK(fdRecKeyDrive("") == 0);
  CHECK(fdRecKeyDrive(nullptr) == 0);
  char b[16];
  CHECK(fdRecKeyBase("A1B2C3D4E5F6:1", b, sizeof b) && std::strcmp(b, "A1B2C3D4E5F6") == 0);
  CHECK(fdRecKeyBase("A1B2C3D4E5F6", b, sizeof b) && std::strcmp(b, "A1B2C3D4E5F6") == 0);
  CHECK(!fdRecKeyBase("A1B2C3D4E5F6:1", b, 12));
}

static void test_df1_record_round_trip_and_old_lines() {
  const FdSaveRecord d1 = fdRec("A1B2C3D4E5F6:1", 77, "/ADF/G.sav.80FE.adf", "/ADF/G.adf", "80FE");
  char line[400];
  CHECK(fdRecordFormat(d1, line, sizeof line) > 0);
  CHECK(std::strcmp(line, "A1B2C3D4E5F6:1\t77\t80FE\t/ADF/G.sav.80FE.adf\t/ADF/G.adf") == 0);
  FdSaveRecord r;
  std::memset(&r, 0, sizeof r);
  CHECK(fdRecordParse(line, r) && fdRecSame(r, d1));
  // a file written before DF1 existed parses as DF0's records, unchanged
  FdSaveRecord old;
  CHECK(fdRecordParse("A1B2C3D4E5F6\t42\t80FE\t/ADF/G.adf\t/ADF/G.adf", old));
  CHECK(std::strcmp(old.id, "A1B2C3D4E5F6") == 0 && fdRecKeyDrive(old.id) == 0);
  // what is not a drive key is refused
  FdSaveRecord keep = old;
  CHECK(!fdRecordParse("A1B2C3D4E5F6:2\t42\t80FE\t/ADF/G.adf\t/ADF/G.adf", r));
  CHECK(!fdRecordParse("A1B2C3D4E5F6:\t42\t80FE\t/ADF/G.adf\t/ADF/G.adf", r));
  CHECK(!fdRecordParse("A1B2C3D4E5F6:0\t42\t80FE\t/ADF/G.adf\t/ADF/G.adf", r));
  CHECK(!fdRecordParse("A1B2C3D4E5F6:11\t42\t80FE\t/ADF/G.adf\t/ADF/G.adf", r));
  CHECK(!fdRecordParse(":1\t42\t80FE\t/ADF/G.adf\t/ADF/G.adf", r));
  CHECK(!fdRecordParse("A1B2C3D4E5F67:1\t42\t80FE\t/ADF/G.adf\t/ADF/G.adf", r));   // 13 hex digits
  CHECK(!fdRecordParse("a1b2c3d4e5f6:1\t42\t80FE\t/ADF/G.adf\t/ADF/G.adf", r));    // lower case
  CHECK(fdRecSame(old, keep));
  // made whole from a fling
  FdSaveRecord s;
  CHECK(fdRecordSet(s, "A1B2C3D4E5F6:1", 9, "/ADF/H.adf", "/ADF/H.adf", "80FE") && std::strcmp(s.id, "A1B2C3D4E5F6:1") == 0);
  CHECK(!fdRecordSet(s, "A1B2C3D4E5F6:1:1", 9, "/ADF/H.adf", "/ADF/H.adf", "80FE"));
  // DF0 and DF1 of one FluxDrive are two records
  FdSaveRecord d0 = fdRec("A1B2C3D4E5F6", 76, "/ADF/F.adf", "/ADF/F.adf", "80FE");
  CHECK(std::strcmp(d0.id, d1.id) != 0);
}

// What the loop fetches next: DF0 first, then DF1 (only while its beacon says df1), each by its own dirty count and uid.
static void test_df1_fetch_due() {
  const FdSaveRecord r0 = fdRec("A1B2C3D4E5F6", 42, "/ADF/G.adf", "/ADF/G.adf", "80FE");
  const FdSaveRecord r1 = fdRec("A1B2C3D4E5F6:1", 43, "/ADF/H.adf", "/ADF/H.adf", "80FE");
  CHECK(fdDueDrive(true, 5, 42, &r0, true, 3, 43, &r1) == 0);          // both: DF0 first
  CHECK(fdDueDrive(true, 0, 42, &r0, true, 3, 43, &r1) == 1);          // only DF1 has writes
  CHECK(fdDueDrive(true, 5, 42, &r0, true, 0, 43, &r1) == 0);
  CHECK(fdDueDrive(true, 0, 42, &r0, true, 0, 43, &r1) == -1);
  CHECK(fdDueDrive(true, 0, 42, &r0, true, 3, 99, &r1) == -1);         // DF1 holds a disk this panel did not send
  CHECK(fdDueDrive(true, 0, 42, &r0, true, 3, 43, nullptr) == -1);     // no record for DF1
  CHECK(fdDueDrive(true, 5, 41, &r0, true, 3, 43, &r1) == 1);          // DF0's record is for another disk, DF1's is fine
  CHECK(fdDueDrive(true, 0, 42, &r0, false, 3, 43, &r1) == -1);        // DF1 off: its keys mean nothing
  CHECK(fdDueDrive(false, 5, 42, &r0, true, 3, 43, &r1) == -1);        // not a FluxDrive, or not heard
}

static void test_df1_bar_text() {
  char o[200];
  CHECK(!fdBarShows(false, false, false, false));
  CHECK(fdBarShows(true, false, false, false) == false);
  CHECK(fdBarShows(true, true, false, false));                          // DF0 as today
  CHECK(fdBarShows(true, true, true, true));
  CHECK(fdBarShows(true, false, true, true));                           // only DF1 holds a disk
  CHECK(!fdBarShows(true, false, false, true));                         // DF1 off: its keys are not looked at
  fdBarCompose("Pinball Dreams", true, "Pinball Dreams 2", true, true, o, sizeof o);
  CHECK(std::strcmp(o, "Pinball Dreams  DF1: Pinball Dreams 2") == 0);
  fdBarCompose("Lemmings", true, "", false, true, o, sizeof o);         // DF1 empty: DF0's disk alone, as today
  CHECK(std::strcmp(o, "Lemmings") == 0);
  fdBarCompose("Lemmings", true, "Ignored", true, false, o, sizeof o);  // DF1 off
  CHECK(std::strcmp(o, "Lemmings") == 0);
  fdBarCompose("", false, "Monkey Island", true, true, o, sizeof o);
  CHECK(std::strcmp(o, "DF1: Monkey Island") == 0);
  fdBarCompose("A", true, "BBBBBBBBBBBB", true, true, o, 10);
  CHECK(std::strlen(o) == 9);                                           // cut to the buffer
}

// ---- Round 2: the DRIVE INFO screen a tap on NOW PLAYING opens ----------------------------------------------------------

static bool rectIs(FdRect r, int x, int y, int w, int h) { return r.x == x && r.y == y && r.w == w && r.h == h; }
static bool rectsOverlap(FdRect a, FdRect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

static void test_drive_info_layout_landscape_with_df1() {
  FdInfoLayout l;
  fdInfoLayout(480, 320, true, true, true, &l);
  CHECK(l.blocks == 2);
  CHECK(rectIs(l.back, 180, 276, 120, 36));
  CHECK(rectIs(l.block[0], 8, 34, 464, 96) && rectIs(l.block[1], 8, 136, 464, 96));
  CHECK(l.hasEject[0] && l.hasEject[1]);
  CHECK(rectIs(l.eject[0], 372, 66, 92, 32) && rectIs(l.eject[1], 372, 168, 92, 32));
  CHECK(rectIs(l.name[0], 16, 52, 348, 74));
  CHECK(!rectsOverlap(l.name[0], l.eject[0]) && !rectsOverlap(l.block[1], l.back));
}

static void test_drive_info_layout_portrait_and_one_drive() {
  FdInfoLayout l;
  fdInfoLayout(320, 480, true, true, true, &l);
  CHECK(rectIs(l.back, 100, 436, 120, 36) && rectIs(l.block[1], 8, 136, 304, 96));
  CHECK(rectIs(l.eject[0], 212, 66, 92, 32) && rectIs(l.name[0], 16, 52, 188, 74));
  fdInfoLayout(320, 480, false, true, true, &l);                         // DF1 not on: one block, DF1's loaded flag does not count
  CHECK(l.blocks == 1 && l.hasEject[0] && !l.hasEject[1]);
  CHECK(fdInfoTap(l, 100, 150) == FD_INFO_NONE);                          // where DF1's block would be
  fdInfoLayout(480, 272, true, true, true, &l);                           // a short screen: the blocks shrink, never touch BACK
  CHECK(l.block[0].h < 96 && l.block[1].y + l.block[1].h <= l.back.y);
}

static void test_drive_info_empty_drive_has_no_eject_or_name() {
  FdInfoLayout l;
  fdInfoLayout(480, 320, true, false, true, &l);
  CHECK(!l.hasEject[0] && l.hasEject[1]);
  CHECK(fdInfoTap(l, 400, 80) == FD_INFO_NONE);                           // where DF0's EJECT would be
  CHECK(fdInfoTap(l, 100, 80) == FD_INFO_NONE);                           // DF0's name: nothing to select
  CHECK(fdInfoTap(l, 400, 180) == FD_INFO_EJECT1);
  CHECK(fdInfoTap(l, 100, 180) == FD_INFO_NAME1);
}

static void test_drive_info_tap() {
  FdInfoLayout l;
  fdInfoLayout(480, 320, true, true, true, &l);
  CHECK(fdInfoTap(l, 200, 290) == FD_INFO_BACK);
  CHECK(fdInfoTap(l, 180, 276) == FD_INFO_BACK && fdInfoTap(l, 299, 311) == FD_INFO_BACK);
  CHECK(fdInfoTap(l, 300, 290) == FD_INFO_NONE && fdInfoTap(l, 200, 275) == FD_INFO_NONE);
  CHECK(fdInfoTap(l, 372, 66) == FD_INFO_EJECT0 && fdInfoTap(l, 463, 97) == FD_INFO_EJECT0);
  CHECK(fdInfoTap(l, 372, 65) == FD_INFO_NONE && fdInfoTap(l, 464, 80) == FD_INFO_NONE);
  CHECK(fdInfoTap(l, 400, 180) == FD_INFO_EJECT1);
  CHECK(fdInfoTap(l, 20, 60) == FD_INFO_NAME0 && fdInfoTap(l, 300, 120) == FD_INFO_NAME0);
  CHECK(fdInfoTap(l, 20, 170) == FD_INFO_NAME1);
  CHECK(fdInfoTap(l, 20, 40) == FD_INFO_NONE);                            // the DF0 caption line
  CHECK(fdInfoTap(l, 2, 2) == FD_INFO_NONE && fdInfoTap(l, 240, 20) == FD_INFO_NONE);
  CHECK(fdInfoTap(l, 479, 319) == FD_INFO_NONE && fdInfoTap(l, -1, 80) == FD_INFO_NONE);
}

// What the screen is for: a FluxDrive target with a disk in a drive (not a dongle, not an empty one). NOW PLAYING never ejects.
static void test_drive_info_opens_from_the_bar_only_when_it_shows() {
  const FdRect bar = { 150, 258, 300, 22 };
  CHECK(fdBarOpensInfo(true, 300, 268, bar, true, fdBarShows(true, false, true, true), false));    // only DF1 holds a disk
  CHECK(!fdBarOpensInfo(true, 300, 268, bar, true, fdBarShows(true, false, false, true), false));  // DF1 off, DF0 empty
}

// The carousel's INSERT key, 170 x 34, is cut like the list's: halves of 83, gap 4.
static void test_carousel_split_key() {
  const FdRect ins = { 155, 244, 170, 34 };
  FdRect h[2];
  fdInsertHalves(ins, 4, h);
  CHECK(rectIs(h[0], 155, 244, 83, 34) && rectIs(h[1], 242, 244, 83, 34));
  CHECK(fdInsertHit(ins, 4, true, 200, 260) == 0 && fdInsertHit(ins, 4, true, 300, 260) == 1 && fdInsertHit(ins, 4, true, 154, 260) == -1);
  CHECK(fdInsertHit(ins, 4, false, 300, 260) == 0);
}

int main() {
  test_fluxdrive_is_told_apart_by_its_beacon_board();
  test_only_a_dd_adf_goes_to_a_fluxdrive();
  test_other_boards_are_never_refused_here();
  test_a_busy_fluxdrive_gets_two_minutes();
  test_a_fluxdrive_stays_listed_through_a_game_load();
  test_the_busy_text();
  test_fd_has_saves_fluxdrive_true_webby_unchanged();
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
  test_a_tap_on_now_playing_opens_the_info();
  test_a_tap_beside_now_playing_does_not_open_it();
  test_only_a_tap_with_a_showing_target_opens_the_info();
  test_only_a_full_wait_without_a_reply_is_busy();
  test_a_fluxdrive_push_gets_up_to_16_segments_of_send_buffer();
  test_every_other_board_keeps_the_default_send_buffer();
  test_the_send_buffer_fits_what_lwip_can_queue();
  test_the_send_buffer_is_raised_by_the_difference();
  test_the_send_buffer_is_left_alone_unless_all_is_as_expected();
  test_a_join_is_logged_once_per_30_s_per_access_point();
  test_the_send_buffer_switch_is_read_from_config_txt();
  test_fd_record_round_trip();
  test_fd_record_rejects_garbage();
  test_fd_record_is_made_whole_or_not_at_all();
  test_fd_should_fetch_needs_dirty_record_and_same_uid();
  test_fd_fetch_before_swap_needs_a_matching_record_not_a_dirty_count();
  test_fd_a_single_fluxdrive_target_gets_its_own_save();
  test_the_row_signature_follows_write_protect();
  test_fd_save_tag_is_four_upper_case_hex_digits();
  test_fd_start_fresh_resends_only_when_that_disk_is_in();
  test_fd_start_fresh_skips_the_fetch_before_its_swap();
  test_fd_wp_reply_meanings();
  test_fd_start_fresh_keeps_every_record_base();
  test_fd_peer_holds_the_disk_of_its_record();
  test_fd_start_fresh_is_offered_in_standalone_and_fleet_mode_only();
  test_fd_after_start_fresh_the_original_is_sent();
  test_fd_fresh_marks_are_added_found_and_dropped();
  test_fd_fresh_mark_lines_round_trip_and_refuse_garbage();
  test_fd_stored_field_missing_counts_as_stored();
  test_fd_storing_shows_only_with_a_disk_not_yet_stored();
  test_the_row_signature_follows_storing();
  test_fd_revert_check_restore_when_the_beacon_reports_prev();
  test_fd_revert_check_drop_prev_when_the_new_uid_is_stored();
  test_fd_revert_check_keep_while_the_new_uid_is_not_stored();
  test_fd_revert_check_keep_without_prev();
  test_fd_revert_check_keep_when_not_loaded();
  test_fd_revert_check_keep_for_anything_else();
  test_fd_prev_list_a_b_c_reverts_to_a_or_b_or_none();
  test_fd_prev_list_drops_the_oldest_when_full();
  test_fd_prev_list_same_game_keeps_nothing();
  test_fd_prev_list_keeps_nothing_else();
  test_fd_save_aside_names();
  test_fd_fresh_pending_is_made_whole_or_not_at_all();
  test_fd_fresh_pending_check();
  test_fd_two_games_started_fresh_in_one_window();
  test_fd_pending_table_full_evicts_the_oldest();
  test_fd_dos_boot_block();
  test_df1_frames();
  test_df1_set_drive_answer();
  test_df1_split_decision();
  test_df1_split_halves_fill_the_button();
  test_df1_insert_hit();
  test_df1_tap_decision();
  test_df1_half_text();
  test_df1_record_key();
  test_df1_record_round_trip_and_old_lines();
  test_df1_fetch_due();
  test_df1_bar_text();
  test_drive_info_layout_landscape_with_df1();
  test_drive_info_layout_portrait_and_one_drive();
  test_drive_info_empty_drive_has_no_eject_or_name();
  test_drive_info_tap();
  test_drive_info_opens_from_the_bar_only_when_it_shows();
  test_carousel_split_key();
  std::printf("%d checks, %d failed\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
