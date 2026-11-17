// Host test for firmware/Gotek_JC3248/calm_ui.h: the calm layout's grid and rectangle helpers.
// Pure C++, no Arduino types. Run with tools/test_calm_ui.sh.
#include "calm_ui.h"
#include <cstdio>

static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { \
  ++g_run; \
  if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void test_constants() {
  CHECK(CU_MARGIN == 8); CHECK(CU_GAP == 8); CHECK(CU_GROUP == 16); CHECK(CU_KEY == 48);
  CHECK(CU_TS_SMALL == 2); CHECK(CU_TS_MID == 3); CHECK(CU_TS_BIG == 4);
  CHECK(cuTextPx(CU_TS_SMALL) == 16); CHECK(cuTextPx(CU_TS_MID) == 24); CHECK(cuTextPx(CU_TS_BIG) == 32);
  CHECK(cuTextSizeOk(2) && cuTextSizeOk(3) && cuTextSizeOk(4));
  CHECK(!cuTextSizeOk(1) && !cuTextSizeOk(5) && !cuTextSizeOk(0));
}

static void test_rect_basics() {
  CuRect r{10, 20, 100, 50};
  CHECK(cuRight(r) == 110); CHECK(cuBottom(r) == 70);
  CHECK(cuContains(r, 10, 20)); CHECK(cuContains(r, 109, 69));
  CHECK(!cuContains(r, 110, 20)); CHECK(!cuContains(r, 10, 70)); CHECK(!cuContains(r, 9, 20)); CHECK(!cuContains(r, 10, 19));
  CuRect i = cuInset(r, 8);
  CHECK(i.x == 18 && i.y == 28 && i.w == 84 && i.h == 34);
  CuRect j = cuInset(r, 8, 4);
  CHECK(j.x == 18 && j.y == 24 && j.w == 84 && j.h == 42);
  CuRect z = cuInset(CuRect{0, 0, 10, 10}, 8);   // never negative
  CHECK(z.w == 0 && z.h == 0);
  CHECK(!cuContains(z, z.x, z.y));
}

static void test_columns() {
  CuRect a{8, 100, 464, 48}, c[4];
  CHECK(cuCols(a, 1, 8, c) == 1);
  CHECK(c[0].x == 8 && c[0].w == 464 && c[0].y == 100 && c[0].h == 48);
  CHECK(cuCols(a, 2, 8, c) == 2);
  CHECK(c[0].x == 8 && c[0].w == 228 && c[1].x == 244 && c[1].w == 228);
  CHECK(cuRight(c[1]) == cuRight(a));
  // odd leftover pixels go to the first columns, nothing is lost, gaps stay exact
  CuRect b{0, 0, 101, 10};
  CHECK(cuCols(b, 2, 8, c) == 2);
  CHECK(c[0].w == 47 && c[1].w == 46 && c[1].x == 55 && cuRight(c[1]) == 101);
  CHECK(cuCols(a, 3, 8, c) == 3);
  CHECK(c[0].w + c[1].w + c[2].w + 16 == 464);
  CHECK(c[1].x == cuRight(c[0]) + 8 && c[2].x == cuRight(c[1]) + 8);
  CHECK(cuCols(a, 0, 8, c) == 0);
}

static void test_rows() {
  CuRect a{8, 40, 200, 3 * 48 + 2 * 8}, r[3];
  CHECK(cuRows(a, 3, 8, r) == 3);
  CHECK(r[0].y == 40 && r[0].h == 48 && r[1].y == 96 && r[2].y == 152 && r[2].h == 48);
  CHECK(r[0].x == 8 && r[0].w == 200);
  CHECK(cuBottom(r[2]) == cuBottom(a));
}

static void test_take() {
  CuRect a{8, 200, 464, 48}, l, r;
  cuTakeRight(a, 72, 8, &l, &r);
  CHECK(r.w == 72 && cuRight(r) == 472 && r.y == 200 && r.h == 48);
  CHECK(l.x == 8 && cuRight(l) == r.x - 8 && l.w == 464 - 72 - 8);
  CuRect t, b;
  cuTakeTop(CuRect{0, 0, 480, 320}, 48, 8, &t, &b);
  CHECK(t.h == 48 && t.y == 0 && b.y == 56 && b.h == 264);
  CuRect m, bt;
  cuTakeBottom(CuRect{0, 0, 480, 320}, 56, 8, &m, &bt);
  CHECK(bt.h == 56 && bt.y == 264 && m.h == 256 && cuBottom(m) == 256);
}

static void test_hit() {
  CuRect rs[3] = {{8, 8, 100, 48}, {116, 8, 100, 48}, {224, 8, 100, 48}};
  CHECK(cuHit(rs, 3, 8, 8) == 0);
  CHECK(cuHit(rs, 3, 107, 55) == 0);
  CHECK(cuHit(rs, 3, 116, 8) == 1);
  CHECK(cuHit(rs, 3, 323, 55) == 2);
  CHECK(cuHit(rs, 3, 112, 20) == -1);   // in the gap
  CHECK(cuHit(rs, 3, 400, 20) == -1);
  CHECK(cuHit(rs, 0, 8, 8) == -1);
  CHECK(cuHit(nullptr, 0, 8, 8) == -1);
  CuRect ov[2] = {{0, 0, 50, 50}, {25, 25, 50, 50}};   // overlap: the one drawn last (topmost) wins
  CHECK(cuHit(ov, 2, 30, 30) == 1);
  CHECK(cuHit(ov, 2, 10, 10) == 0);
}

static void test_touch_area() {
  CuRect small{100, 100, 20, 14};
  CuRect t = cuTouchArea(small);   // the hit rectangle is at least 48 x 48, centred on the drawing
  CHECK(t.w == 48 && t.h == 48 && t.x == 86 && t.y == 83);
  CuRect big{0, 0, 200, 60};
  CuRect u = cuTouchArea(big);
  CHECK(u.w == 200 && u.h == 60 && u.x == 0 && u.y == 0);
  CuRect w = cuTouchArea(CuRect{0, 0, 64, 20});
  CHECK(w.w == 64 && w.h == 48 && w.y == -14);
}

static void test_stepper() {
  CuRect a{8, 100, 304, 48}, l, m, r;
  cuStepper(a, &l, &m, &r);   // [<] text [>]: two 48 x 48 arrows, the text between, gaps of 8
  CHECK(l.x == 8 && l.w == 48 && l.h == 48 && l.y == 100);
  CHECK(r.w == 48 && cuRight(r) == 312);
  CHECK(m.x == 64 && cuRight(m) == r.x - 8 && m.w == 304 - 96 - 16);
  CuRect tall{0, 0, 200, 64};   // arrows keep 48 wide and fill the height
  cuStepper(tall, &l, &m, &r);
  CHECK(l.h == 64 && m.h == 64 && r.h == 64);
}

int main() {
  test_constants();
  test_rect_basics();
  test_columns();
  test_rows();
  test_take();
  test_hit();
  test_touch_area();
  test_stepper();
  std::printf("%d checks, %d failed\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
