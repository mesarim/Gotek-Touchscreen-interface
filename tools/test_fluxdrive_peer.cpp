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
  CHECK(!fdSameDiskName(nullptr, "Lemmings"));
  CHECK(!fdSameDiskName("Lemmings", nullptr));
  CHECK(!fdSameDiskName(nullptr, nullptr));
}

// What FluxDrive keeps as the disk's name for a name the panel sends with SET_NAME (pfSendName cuts it to 120 bytes):
// it cuts at the last dot (it takes it for an extension), keeps 95 characters, and calls a disk sent without a name
// "DISK.ADF", so "DISK".
static void test_the_name_fluxdrive_keeps_for_a_sent_name() {
  char b[128];
  CHECK(fdDriveDiskName("Lemmings", b, sizeof(b)) == 8 && std::strcmp(b, "Lemmings") == 0);
  CHECK(fdDriveDiskName("Workbench 1.3", b, sizeof(b)) == 11 && std::strcmp(b, "Workbench 1") == 0);
  CHECK(fdDriveDiskName("Monkey.Island.2", b, sizeof(b)) > 0 && std::strcmp(b, "Monkey.Island") == 0);   // the LAST dot
  CHECK(fdDriveDiskName(".hidden", b, sizeof(b)) > 0 && std::strcmp(b, ".hidden") == 0);                // a dot at 0 stays
  CHECK(fdDriveDiskName("", b, sizeof(b)) == 4 && std::strcmp(b, "DISK") == 0);
  CHECK(fdDriveDiskName(nullptr, b, sizeof(b)) == 4 && std::strcmp(b, "DISK") == 0);
}

static void test_the_kept_name_is_cut_like_fluxdrive_cuts_it() {
  char sent[200], b[200];
  std::memset(sent, 'a', 130); sent[130] = 0;                      // no dot: 95 characters are kept
  CHECK(fdDriveDiskName(sent, b, sizeof(b)) == 95 && std::strlen(b) == 95);
  std::memset(sent, 'a', 130); sent[125] = '.'; sent[130] = 0;     // the dot is past the 120 the panel sends: not cut there
  CHECK(fdDriveDiskName(sent, b, sizeof(b)) == 95 && std::strchr(b, '.') == nullptr);
  std::memset(sent, 'a', 130); sent[100] = '.'; sent[130] = 0;     // cut at the dot (100), then to 95
  CHECK(fdDriveDiskName(sent, b, sizeof(b)) == 95);
  std::memset(sent, 'a', 130); sent[40] = '.'; sent[130] = 0;      // cut at the dot
  CHECK(fdDriveDiskName(sent, b, sizeof(b)) == 40);
  std::memset(sent, 'a', 130); sent[40] = '.'; sent[125] = '.'; sent[130] = 0;   // the last dot FluxDrive sees is at 40:
  CHECK(fdDriveDiskName(sent, b, sizeof(b)) == 40);                               // the one at 125 never left the panel
  char tiny[6];
  CHECK(fdDriveDiskName("Workbench 1.3", tiny, sizeof(tiny)) == 5 && std::strcmp(tiny, "Workb") == 0);   // never past the end
  CHECK(fdDriveDiskName("x", tiny, 0) == 0);
  CHECK(fdDriveDiskName("x", nullptr, 8) == 0);
}

// Does the disk a FluxDrive reports hold the game the panel sends under `sent`? Either the name as sent (a disk put in
// another way, under the same name) or the name FluxDrive made of it (a panel fling).
static void test_the_drive_holds_the_disk_sent_under_a_name() {
  CHECK(fdHoldsSentName("Workbench 1", "Workbench 1.3"));          // a panel fling of "Workbench 1.3"
  CHECK(fdHoldsSentName("workbench 1 ", "Workbench 1.3"));
  CHECK(fdHoldsSentName("Workbench 1.3", "Workbench 1.3"));        // the same disk put in by name, from elsewhere
  CHECK(fdHoldsSentName("Workbench 1.3.adf", "Workbench 1.3"));    // ... by FluxDrive's web page, with its file name
  CHECK(fdHoldsSentName("Lemmings", "Lemmings"));
  CHECK(!fdHoldsSentName("Workbench 1.3", "Workbench 1"));         // the other way round is another disk
  CHECK(!fdHoldsSentName("Lemmings", "Lemmings 2"));
  CHECK(!fdHoldsSentName("", "Lemmings"));                         // an empty drive holds nothing
  CHECK(!fdHoldsSentName(nullptr, "Lemmings"));
  CHECK(fdHoldsSentName("DISK", ""));                              // a disk sent with no name is "DISK" to FluxDrive
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
  test_the_name_fluxdrive_keeps_for_a_sent_name();
  test_the_kept_name_is_cut_like_fluxdrive_cuts_it();
  test_the_drive_holds_the_disk_sent_under_a_name();
  std::printf("%d checks, %d failed\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
