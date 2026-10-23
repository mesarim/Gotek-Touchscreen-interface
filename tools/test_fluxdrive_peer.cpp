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

int main() {
  test_fluxdrive_is_told_apart_by_its_beacon_board();
  test_only_a_dd_adf_goes_to_a_fluxdrive();
  test_other_boards_are_never_refused_here();
  test_a_busy_fluxdrive_gets_two_minutes();
  test_the_busy_text();
  test_a_fluxdrive_has_no_saves();
  test_the_device_word_in_messages();
  test_the_board_kind_from_the_pairing_reply();
  test_the_mdns_prefix_of_a_paired_device();
  std::printf("%d checks, %d failed\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
