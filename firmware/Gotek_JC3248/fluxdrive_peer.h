// fluxdrive_peer.h - what the panel decides from a fleet peer's board.
//
// FluxDrive is an ESP32-S3 board that IS drive DF0 of an Amiga. It joins the fleet like a Webby dongle: same UDP
// beacons (51703), same TCP push (3333), same ESP-NOW pairing. It tells itself apart by "board":"fluxdrive" in its
// beacon and by board kind 4 in pad[2] of the pairing reply (Webby: 1 SuperMini, 2 Zero, 3 XIAO).
//
// What differs for the panel:
//  - it takes ONE image size, a DD ADF of 901,120 bytes, and refuses anything else before any change;
//  - while the Amiga's drive motor runs it neither accepts nor answers, so a push can sit in the TCP backlog for
//    the length of a game load, and so can the reply to SET_NAME;
//  - it never writes a disk, so there are no saves to pull and none to lose by a take-over.
//
// Pure functions of the board string, no Arduino types: tools/test_fluxdrive_peer.sh builds and runs them on the PC.
// Every other board gets exactly the answer the panel gave before this file existed.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define FD_KIND_FLUXDRIVE 4              // pad[2] of the pairing reply / board_id in /status
#define FD_DD_ADF_BYTES   901120u        // 80 cylinders x 2 heads x 11 sectors x 512 bytes
#define FD_BUSY_WAIT_MS   120000u        // longer than any game load the Amiga makes a push wait for
#define FD_STALE_MS       180000u        // silence a FluxDrive may keep and still be listed: more than the wait above

inline bool fdIsFluxDrive(const char* board) { return board != nullptr && strcmp(board, "fluxdrive") == 0; }

// nullptr = fine to send; otherwise the text the user gets, and nothing may be connected or sent.
inline const char* fdRefuseImage(const char* board, uint32_t bytes) {
  if (fdIsFluxDrive(board) && bytes != FD_DD_ADF_BYTES) return "FluxDrive takes DD ADFs only (901,120 bytes)";
  return nullptr;
}

// How long a push may make no progress (15 s for a dongle) before the panel gives up.
inline uint32_t fdStallLimitMs(const char* board) { return fdIsFluxDrive(board) ? FD_BUSY_WAIT_MS : 15000u; }

// How long to wait for a reply (the ack, the SET_NAME answer). normal_ms is what the panel waits for a dongle.
inline uint32_t fdReplyWaitMs(const char* board, uint32_t normal_ms) { return fdIsFluxDrive(board) ? FD_BUSY_WAIT_MS : normal_ms; }

// How long a peer may stay silent before the panel drops it from the fleet (40 s for a dongle, which beacons every 12 s).
// A FluxDrive sends no beacon while the Amiga's motor runs, so a game load must not make it vanish from the list.
inline uint32_t fdStaleMs(const char* board, uint32_t normal_ms) { return fdIsFluxDrive(board) ? FD_STALE_MS : normal_ms; }

// What a stalled push means for a FluxDrive: not a broken link, the Amiga is using the drive.
inline const char* fdBusyText() { return "FluxDrive busy - the Amiga is reading"; }

// No write-back in v1: no save pull, and no "saves not handed back" to refuse or warn about.
inline bool fdHasSaves(const char* board) { return !fdIsFluxDrive(board); }

// "dongle" in the words the panel says to the user; a FluxDrive is not one.
inline const char* fdDeviceWord(const char* board) { return fdIsFluxDrive(board) ? "FluxDrive" : "dongle"; }

// The board string a pairing reply's kind number stands for ("" = a Webby, or an old pairing that never kept it).
inline const char* fdBoardForKind(uint8_t kind) { return kind == FD_KIND_FLUXDRIVE ? "fluxdrive" : ""; }

// The mDNS name of a paired device is <prefix>-<mac bytes 4 and 5>.local: a Webby is gotekomega-, a FluxDrive fluxdrive-.
inline const char* fdHostPrefix(uint8_t kind) { return kind == FD_KIND_FLUXDRIVE ? "fluxdrive" : "gotekomega"; }

// ---- Fix round 1: what the person at the touchscreen sees while a FluxDrive is busy -------------------------------

#define FD_BUSY_MEMO_MS   10000u         // how long "it did not answer SET_NAME" is remembered for the push right behind it

// SET_NAME is the first thing sent for a disk. A FluxDrive that stayed silent for the WHOLE wait (not one whose
// connection closed early) is still busy, so the push behind it would only wait out the same game load again.
inline bool fdNameMeansBusy(const char* board, bool got_reply, bool wait_ran_out) {
  return fdIsFluxDrive(board) && !got_reply && wait_ran_out;
}

// One remembered address. note() when SET_NAME ran out; take() in the push to the same address says "busy" at once,
// once, and only while the note is fresh. clear() starts a new send. now is millis(); the wrap is handled.
struct FdBusyMemo {
  char ip[48];
  uint32_t at;
  bool set;
  FdBusyMemo() : at(0), set(false) { ip[0] = 0; }
  void clear() { set = false; ip[0] = 0; }
  void note(const char* addr, uint32_t now) {
    strncpy(ip, addr ? addr : "", sizeof(ip) - 1);
    ip[sizeof(ip) - 1] = 0;
    at = now;
    set = true;
  }
  bool take(const char* addr, uint32_t now, uint32_t ttl_ms) {
    if (!set) return false;
    if ((uint32_t)(now - at) > ttl_ms) { clear(); return false; }
    if (addr == nullptr || ip[0] == 0 || strncmp(ip, addr, sizeof(ip) - 1) != 0) return false;
    clear();
    return true;
  }
};

// Multi-select result line: "<name>: <text>" is centred without wrapping, so it clips at both ends when too wide.
// A FluxDrive's refusal is 44 characters; the name in front makes it about 60. For a FluxDrive the name is dropped
// when there is one target, or when the full line does not fit the screen (8 px margin each side).
// Every other board keeps "<name>: <text>" exactly as before.
inline bool fdKeepNamePrefix(const char* board, int targets, int full_text_px, int screen_px) {
  if (!fdIsFluxDrive(board)) return true;
  return targets > 1 && full_text_px <= screen_px - 8;
}

// What the fleet manager compares to decide whether to repaint: for a FluxDrive the Amiga's power, whether it holds a
// disk and the disk's name; for any other board nothing (its row repaints when its id list changes, as before).
// Writes "" or "|<A|a><L|l><disk>;" into out (cut to cap-1) and returns the length.
inline size_t fdRowSig(const char* board, bool amiga, bool loaded, const char* disk, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = 0;
  if (!fdIsFluxDrive(board)) return 0;
  const int n = snprintf(out, cap, "|%c%c%s;", amiga ? 'A' : 'a', loaded ? 'L' : 'l', disk ? disk : "");
  if (n < 0) { out[0] = 0; return 0; }
  return (size_t)n >= cap ? cap - 1 : (size_t)n;
}
