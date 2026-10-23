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
#include <string.h>

#define FD_KIND_FLUXDRIVE 4              // pad[2] of the pairing reply / board_id in /status
#define FD_DD_ADF_BYTES   901120u        // 80 cylinders x 2 heads x 11 sectors x 512 bytes
#define FD_BUSY_WAIT_MS   120000u        // longer than any game load the Amiga makes a push wait for

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
