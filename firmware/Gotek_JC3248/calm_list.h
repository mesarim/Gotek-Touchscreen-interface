// calm_list.h - the calm list's decisions (docs/ui/README.md, plan phase 3): the search row, the 48 px rows, the letter grid,
// the chip at the right of a row, and the short form of a multi-disk name. Pure functions, no Arduino types:
// tools/test_calm_list.sh builds and runs them on the PC.
#pragma once
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "calm_ui.h"

// ---- Search row: [ Search ........ ] [A-Z] [filter] -------------------------------------------------------------------
#define CL_KEY_W 72
struct ClSearchRow { CuRect search, az, filter; };
inline ClSearchRow clSearchRow(const CuRect& area) {
  ClSearchRow r;
  r.filter = CuRect{cuRight(area) - CL_KEY_W, area.y, CL_KEY_W, area.h};
  r.az     = CuRect{r.filter.x - CU_GAP - CL_KEY_W, area.y, CL_KEY_W, area.h};
  r.search = CuRect{area.x, area.y, r.az.x - CU_GAP - area.x, area.h};
  return r;
}

// ---- Rows: 48 px drawn, a pitch of 52 (a gap of 4); the gap belongs to the row above it -------------------------------
#define CL_ROW_H     48
#define CL_ROW_PITCH 52
inline int clMaxScroll(int n, int viewH) { const int t = n * CL_ROW_PITCH - viewH; return t > 0 ? t : 0; }
inline int clFirstVisible(int scroll) { return scroll <= 0 ? 0 : scroll / CL_ROW_PITCH; }
inline int clRowAt(int scroll, int listTop, int y, int n) {
  const int rel = y - listTop + scroll;
  if (rel < 0) return -1;
  const int i = rel / CL_ROW_PITCH;
  return i < n ? i : -1;
}
inline CuRect clRowRect(int i, int scroll, int listTop, int x, int w) { return CuRect{x, listTop + i * CL_ROW_PITCH - scroll, w, CL_ROW_H}; }

// ---- The letter grid (mockup 6): 7 columns of 64 x 48, A..Z then '#' ------------------------------------------------
#define CL_LETTERS "ABCDEFGHIJKLMNOPQRSTUVWXYZ#"
#define CL_LETTER_N 27
#define CL_CELL_W 64
#define CL_CELL_GAP 2
inline int clLetterCols(int w) { return w >= 460 ? 7 : 4; }   // 7 columns on 480, 4 on 320
inline CuRect clLetterCell(int i, int w, int top) {
  const int cols = clLetterCols(w), col = i % cols, row = i / cols, x0 = (w - (cols * CL_CELL_W + (cols - 1) * CL_CELL_GAP)) / 2;
  return CuRect{x0 + col * (CL_CELL_W + CL_CELL_GAP), top + row * (CU_KEY + CL_CELL_GAP), CL_CELL_W, CU_KEY};
}
inline int clLetterAt(int w, int top, int x, int y) {
  for (int i = 0; i < CL_LETTER_N; i++) if (cuContains(clLetterCell(i, w, top), x, y)) return i;
  return -1;
}
// The index of a letter in CL_LETTERS; anything that is not a letter is the '#' bucket (bucketOf's rule).
inline int clLetterIndex(int c) {
  if (c >= 'a' && c <= 'z') return c - 'a';
  if (c >= 'A' && c <= 'Z') return c - 'A';
  return CL_LETTER_N - 1;
}

// ---- "<game> - Disk <n>" shown as "<game> . D<n>" (the dot is drawn as a real dot) ------------------------------------
// true: name ended in " - Disk <n>"; game gets the part before it (cut to cap-1), *disk the number. false: game = name, *disk = 0.
inline bool clSplitDisk(const char* name, char* game, size_t cap, int* disk) {
  if (disk) *disk = 0;
  if (cap == 0) return false;
  game[0] = 0;
  if (name == nullptr) return false;
  const size_t n = strlen(name);
  bool ok = false; size_t cut = n; int num = 0;
  // scan back over the digits, then expect " - Disk " in front of them and a game part before that
  size_t d = n;
  while (d > 0 && name[d - 1] >= '0' && name[d - 1] <= '9') d--;
  static const char tag[] = " - Disk ";
  const size_t tl = sizeof tag - 1;
  if (d < n && d >= tl + 1 && memcmp(name + d - tl, tag, tl) == 0 && n - d <= 4) {
    ok = true; cut = d - tl;
    for (size_t k = d; k < n; k++) num = num * 10 + (name[k] - '0');
  }
  size_t keep = cut < cap - 1 ? cut : cap - 1;
  memcpy(game, name, keep); game[keep] = 0;
  if (ok && disk) *disk = num;
  return ok;
}
inline int clTailChars(int digits) { return 3 + 1 + digits; }   // " . D" and the number
// How many characters of the game part fit beside the tail in widthPx at charPx a character (never negative, never more than gameLen).
inline int clFitDisk(int gameLen, int charPx, int widthPx, int digits) {
  int room = widthPx / charPx - clTailChars(digits);
  if (room < 0) room = 0;
  return room < gameLen ? room : gameLen;
}

// ---- The chip at the right of a row ----------------------------------------------------------------------------------
// loaded somewhere: which drive ("DF0", "DF1", "DF0 DF1" when the FluxDrive's two drives show; a dongle's own name; "loaded"
// for the wired Gotek); else more than one disk: "<n> disks"; else nothing.
enum { CL_CHIP_NONE = 0, CL_CHIP_DISKS, CL_CHIP_LOADED };
struct ClChip { int kind; char text[16]; };
inline ClChip clRowChip(int diskCount, bool splitPeer, bool df0, bool df1, bool anyLoaded, const char* singleName,
                        const char* disksWord = "disks", const char* loadedWord = "loaded") {   // the words are translated by the caller
  ClChip c; c.kind = CL_CHIP_NONE; c.text[0] = 0;
  if (splitPeer && (df0 || df1)) {
    c.kind = CL_CHIP_LOADED;
    strcpy(c.text, df0 && df1 ? "DF0 DF1" : (df0 ? "DF0" : "DF1"));
  } else if (!splitPeer && anyLoaded) {
    c.kind = CL_CHIP_LOADED;
    if (singleName != nullptr && singleName[0]) { strncpy(c.text, singleName, 9); c.text[9] = 0; } else { strncpy(c.text, loadedWord, sizeof c.text - 1); c.text[sizeof c.text - 1] = 0; }
  } else if (diskCount > 1) {
    c.kind = CL_CHIP_DISKS;
    snprintf(c.text, sizeof c.text, "%d %s", diskCount, disksWord);
  }
  return c;
}

// ---- Settings (A600-calm6, mockup 5): Back, four tabs, setting rows of 56 px at a pitch of 60, a confirmation screen -------------
#define CL_SET_H     56
#define CL_SET_PITCH 60
struct ClSetTabs { CuRect back; CuRect tab[4]; int listTop; };   // listTop: where the rows start
inline ClSetTabs clSettingsTabs(int w) {
  ClSetTabs t;
  t.back = CuRect{CU_MARGIN, 4, CU_KEY, CU_KEY};
  if (w >= 400) {   // landscape: the tabs beside Back
    const CuRect area{cuRight(t.back) + CU_GAP, 4, w - CU_MARGIN - (cuRight(t.back) + CU_GAP), CU_KEY};
    cuCols(area, 4, 0, t.tab); t.listTop = 60;
  } else {          // portrait: the tabs in a row of their own under Back
    const CuRect area{CU_MARGIN, cuBottom(t.back) + 4, w - 2 * CU_MARGIN, CU_KEY};
    cuCols(area, 4, 0, t.tab); t.listTop = cuBottom(area) + CU_GAP;
  }
  return t;
}
inline int clSetTabAt(const ClSetTabs& t, int x, int y) {
  for (int i = 0; i < 4; i++) if (cuContains(t.tab[i], x, y)) return i;
  return -1;
}
inline int clSetMaxScroll(int n, int viewH) { const int v = n * CL_SET_PITCH - viewH; return v > 0 ? v : 0; }
inline int clSetRowAt(int scroll, int listTop, int y, int n) {
  const int rel = y - listTop + scroll;
  if (rel < 0) return -1;
  const int i = rel / CL_SET_PITCH;
  return i < n ? i : -1;
}
inline CuRect clSetRowRect(int i, int scroll, int listTop, int x, int w) { return CuRect{x, listTop + i * CL_SET_PITCH - scroll, w, CL_SET_H}; }

// A confirmation: Back at the top, the message, and at the foot Cancel (left) and the action (right), 16 px apart, 48 high, far from Back.
struct ClConfirm { CuRect back, msg, cancel, ok; };
enum { CL_CF_NONE = 0, CL_CF_CANCEL, CL_CF_OK };
inline ClConfirm clConfirmLayout(int w, int h) {
  ClConfirm c;
  c.back = CuRect{CU_MARGIN, 4, 104, CU_KEY};
  const int ky = h - CU_MARGIN - CU_KEY, half = (w - 2 * CU_MARGIN - CU_GROUP) / 2;
  c.cancel = CuRect{CU_MARGIN, ky, half, CU_KEY};
  c.ok     = CuRect{w - CU_MARGIN - half, ky, half, CU_KEY};
  c.msg    = CuRect{CU_MARGIN, cuBottom(c.back) + CU_GAP, w - 2 * CU_MARGIN, ky - CU_GAP - (cuBottom(c.back) + CU_GAP)};
  return c;
}
inline int clConfirmTap(const ClConfirm& c, int x, int y) {
  if (cuContains(c.ok, x, y)) return CL_CF_OK;
  if (cuContains(c.cancel, x, y) || cuContains(c.back, x, y)) return CL_CF_CANCEL;
  return CL_CF_NONE;
}
