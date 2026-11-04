// calm_ui.h - the calm layout's grid and rectangle helpers (docs/ui/README.md, docs/ui/plan.md phase 1).
//
// Rules: every touch area at least 48 x 48 px (the drawing may be smaller, the hit rectangle is not); an 8 px grid
// (margins 8, gaps 8, between groups 16); the pixel font at three sizes only, setTextSize 2 / 3 / 4 = 16 / 24 / 32 px.
//
// Pure functions, no Arduino types: tools/test_calm_ui.sh builds and runs them on the PC. The sketch's drawing code
// ("calm UI primitives") and gti_targets.h build on these.
#pragma once
#include <stddef.h>

#define CU_MARGIN  8     // screen edge to content
#define CU_GAP     8     // between neighbours
#define CU_GROUP   16    // between groups
#define CU_KEY     48    // touch size: key height, minimum hit width and height, list row height

#define CU_TS_SMALL 2    // 16 px
#define CU_TS_MID   3    // 24 px
#define CU_TS_BIG   4    // 32 px

inline int  cuTextPx(int size)     { return size * 8; }                       // the pixel font's cell is 8 px at size 1
inline bool cuTextSizeOk(int size) { return size >= CU_TS_SMALL && size <= CU_TS_BIG; }

struct CuRect { int x, y, w, h; };

inline int  cuRight(const CuRect& r)  { return r.x + r.w; }
inline int  cuBottom(const CuRect& r) { return r.y + r.h; }
inline bool cuContains(const CuRect& r, int px, int py) { return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h; }

// Shrink by dx on the left and right, dy on top and bottom (never below 0 wide / high).
inline CuRect cuInset(const CuRect& r, int dx, int dy) {
  CuRect o{r.x + dx, r.y + dy, r.w - 2 * dx, r.h - 2 * dy};
  if (o.w < 0) o.w = 0;
  if (o.h < 0) o.h = 0;
  return o;
}
inline CuRect cuInset(const CuRect& r, int d) { return cuInset(r, d, d); }

// Split r into n columns (rows) with gap between them. Leftover pixels go one each to the first ones, so the last
// column ends exactly at r's edge. out[] holds n rects; returns n (0 for n < 1).
inline int cuCols(const CuRect& r, int n, int gap, CuRect* out) {
  if (n < 1) return 0;
  const int room = r.w - gap * (n - 1), base = room / n, extra = room % n;
  int x = r.x;
  for (int i = 0; i < n; i++) { const int w = base + (i < extra ? 1 : 0); out[i] = CuRect{x, r.y, w, r.h}; x += w + gap; }
  return n;
}
inline int cuRows(const CuRect& r, int n, int gap, CuRect* out) {
  if (n < 1) return 0;
  const int room = r.h - gap * (n - 1), base = room / n, extra = room % n;
  int y = r.y;
  for (int i = 0; i < n; i++) { const int h = base + (i < extra ? 1 : 0); out[i] = CuRect{r.x, y, r.w, h}; y += h + gap; }
  return n;
}

// Cut a fixed-size piece off one side of r: the piece (w wide / h high) and what is left after a gap.
inline void cuTakeRight(const CuRect& r, int w, int gap, CuRect* rest, CuRect* piece) {
  *piece = CuRect{r.x + r.w - w, r.y, w, r.h};
  *rest  = CuRect{r.x, r.y, r.w - w - gap, r.h};
}
inline void cuTakeTop(const CuRect& r, int h, int gap, CuRect* piece, CuRect* rest) {
  *piece = CuRect{r.x, r.y, r.w, h};
  *rest  = CuRect{r.x, r.y + h + gap, r.w, r.h - h - gap};
}
inline void cuTakeBottom(const CuRect& r, int h, int gap, CuRect* rest, CuRect* piece) {
  *piece = CuRect{r.x, r.y + r.h - h, r.w, h};
  *rest  = CuRect{r.x, r.y, r.w, r.h - h - gap};
}

// Which rect of the list a tap is in: -1 for none. Overlapping rects: the last one in the list (drawn on top) wins.
inline int cuHit(const CuRect* rs, int n, int px, int py) {
  if (rs == nullptr) return -1;
  for (int i = n - 1; i >= 0; i--) if (cuContains(rs[i], px, py)) return i;
  return -1;
}

// The hit rectangle for a drawing: at least 48 x 48, centred on it (a larger drawing keeps its own size).
inline CuRect cuTouchArea(const CuRect& r) {
  CuRect t = r;
  if (t.w < CU_KEY) { t.x -= (CU_KEY - t.w) / 2; t.w = CU_KEY; }
  if (t.h < CU_KEY) { t.y -= (CU_KEY - t.h) / 2; t.h = CU_KEY; }
  return t;
}

// A stepper "[<] text [>]": two arrow keys CU_KEY wide at the ends, the text field between them, gaps of CU_GAP.
inline void cuStepper(const CuRect& r, CuRect* left, CuRect* mid, CuRect* right) {
  *left  = CuRect{r.x, r.y, CU_KEY, r.h};
  *right = CuRect{r.x + r.w - CU_KEY, r.y, CU_KEY, r.h};
  *mid   = CuRect{r.x + CU_KEY + CU_GAP, r.y, r.w - 2 * (CU_KEY + CU_GAP), r.h};
  if (mid->w < 0) mid->w = 0;
}
