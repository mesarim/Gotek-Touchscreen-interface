// Host test for firmware/Gotek_JC3248/calm_list.h (the calm list: search row, 48 px rows, the letter grid, the row chip, the
// short "<game> . D<n>" form) and firmware/Gotek_JC3248/game_sort.h (the library sort). Pure C++. Run with tools/test_calm_list.sh.
#include "calm_list.h"
#include "game_sort.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>

static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { \
  ++g_run; \
  if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void test_search_row() {
  ClSearchRow r = clSearchRow(CuRect{8, 48, 464, 48});
  CHECK(r.filter.w == 72 && cuRight(r.filter) == 472 && r.filter.h == 48 && r.filter.y == 48);
  CHECK(r.az.w == 72 && cuRight(r.az) == r.filter.x - 8);
  CHECK(r.star.w == 48 && r.star.h == 48 && cuRight(r.star) == r.az.x - 8);      // the favourites key: 48 wide, between Search and A-Z
  CHECK(r.search.x == 8 && cuRight(r.search) == r.star.x - 8 && r.search.w == 464 - 72 - 72 - 48 - 24);
  CuRect rs[4] = {r.search, r.star, r.az, r.filter};
  CHECK(cuHit(rs, 4, 100, 60) == 0 && cuHit(rs, 4, r.star.x + 5, 60) == 1 && cuHit(rs, 4, r.az.x + 5, 60) == 2 && cuHit(rs, 4, 450, 90) == 3);
  CHECK(cuHit(rs, 4, 100, 100) == -1);
  for (const CuRect& k : rs) CHECK(k.w >= 48 && k.h >= 48);   // every key at least 48 wide
}

static void test_rows() {
  CHECK(CL_ROW_H == 48 && CL_ROW_PITCH == 52);
  // list window from y 104 to 256 (152 px): 2 full rows of pitch 52 plus most of a third
  CHECK(clMaxScroll(0, 152) == 0 && clMaxScroll(2, 152) == 0 && clMaxScroll(3, 152) == 4);
  CHECK(clMaxScroll(95, 152) == 95 * 52 - 152);
  CHECK(clRowAt(0, 104, 104, 95) == 0 && clRowAt(0, 104, 155, 95) == 0 && clRowAt(0, 104, 156, 95) == 1);
  CHECK(clRowAt(0, 104, 103, 95) == -1);   // above the list
  CHECK(clRowAt(100, 104, 104, 95) == 1);  // scrolled: 100 px = row 1 (pitch 52) starts at 52, 100 is inside row 1
  CHECK(clRowAt(0, 104, 255, 2) == -1);    // below the last row
  CHECK(clRowAt(0, 104, 104 + 52 + 20, 2) == 1);
  CuRect r = clRowRect(2, 30, 104, 8, 464);
  CHECK(r.x == 8 && r.w == 464 && r.h == 48 && r.y == 104 + 2 * 52 - 30);
  CHECK(clFirstVisible(0) == 0 && clFirstVisible(51) == 0 && clFirstVisible(52) == 1 && clFirstVisible(-5) == 0);
}

static void test_letter_grid() {
  CHECK(std::strlen(CL_LETTERS) == 27 && CL_LETTERS[0] == 'A' && CL_LETTERS[25] == 'Z' && CL_LETTERS[26] == '#');
  CuRect c0 = clLetterCell(0, 480, 60), c6 = clLetterCell(6, 480, 60), c7 = clLetterCell(7, 480, 60), c26 = clLetterCell(26, 480, 60);
  CHECK(c0.w == 64 && c0.h == 48 && c0.y == 60);
  CHECK(c0.x == 10 && cuRight(c6) == 470);   // 7 cells of 64 and gaps of 2 centred on 480
  CHECK(c7.y == 60 + 50 && c7.x == c0.x);
  CHECK(c26.y == 60 + 3 * 50 && c26.x == c0.x + 5 * 66);   // '#' is the 27th: row 4, column 6
  CHECK(cuBottom(c26) <= 320 - 8);
  CHECK(clLetterAt(480, 60, c0.x + 5, c0.y + 5) == 0);
  CHECK(clLetterAt(480, 60, c26.x + 63, c26.y + 47) == 26);
  CHECK(clLetterAt(480, 60, c6.x + 64, c6.y + 5) == -1);   // the 2 px gap
  CHECK(clLetterAt(480, 60, 5, 100) == -1);                 // left of the grid
  CHECK(clLetterAt(480, 60, 475, 100) == -1);               // right of it
  CHECK(clLetterAt(480, 60, c7.x + 5, c7.y + 5) == 7);
  // the 28th slot is empty
  CuRect slot27{c0.x + 6 * 66, 60 + 3 * 50, 64, 48};
  CHECK(clLetterAt(480, 60, slot27.x + 5, slot27.y + 5) == -1);
  CHECK(clLetterIndex('A') == 0 && clLetterIndex('z') == 25 && clLetterIndex('#') == 26 && clLetterIndex('5') == 26 && clLetterIndex(0) == 26);
}

static void test_split_disk() {
  char g[64]; int d;
  CHECK(clSplitDisk("Lemmings 1 - Disk 2", g, sizeof g, &d) && std::strcmp(g, "Lemmings 1") == 0 && d == 2);
  CHECK(clSplitDisk("Monkey Island - Disk 12", g, sizeof g, &d) && std::strcmp(g, "Monkey Island") == 0 && d == 12);
  CHECK(!clSplitDisk("Lemmings 1-1", g, sizeof g, &d) && std::strcmp(g, "Lemmings 1-1") == 0 && d == 0);
  CHECK(!clSplitDisk("Disk 2", g, sizeof g, &d) && std::strcmp(g, "Disk 2") == 0);   // no game part: not a suffix
  CHECK(!clSplitDisk("A - Disk x", g, sizeof g, &d) && d == 0);
  CHECK(!clSplitDisk("A - Disk 2 more", g, sizeof g, &d));
  CHECK(!clSplitDisk("A - Disk ", g, sizeof g, &d));
  CHECK(!clSplitDisk("", g, sizeof g, &d) && g[0] == 0);
  CHECK(!clSplitDisk(nullptr, g, sizeof g, &d) && g[0] == 0);
  char small[6];
  CHECK(clSplitDisk("Lemmings - Disk 1", small, sizeof small, &d) && std::strcmp(small, "Lemmi") == 0 && d == 1);   // cut, never overrun
  // the truncation: the game part gives way, " . D<n>" stays
  CHECK(clFitDisk(12, 6, 150, 1) == 12);             // room for the whole thing
  CHECK(clFitDisk(30, 6, 150, 1) == 20);             // 150 px at 6 px a char = 25 chars; the tail " . D2" is 5 of them
  CHECK(clFitDisk(30, 6, 150, 2) == 19);             // " . D12": one more
  CHECK(clFitDisk(30, 6, 20, 1) == 0);               // no room even for the tail: the game part goes
  CHECK(clFitDisk(0, 6, 150, 1) == 0);
  CHECK(clTailChars(1) == 5 && clTailChars(2) == 6);
}

static void test_row_chip() {
  ClChip c = clRowChip(1, false, false, false, false, "");
  CHECK(c.kind == CL_CHIP_NONE);
  c = clRowChip(3, false, false, false, false, "");
  CHECK(c.kind == CL_CHIP_DISKS && std::strcmp(c.text, "3 disks") == 0);
  c = clRowChip(2, false, false, false, false, "");
  CHECK(c.kind == CL_CHIP_DISKS && std::strcmp(c.text, "2 disks") == 0);
  c = clRowChip(2, false, false, false, true, "");   // loaded in the wired Gotek: it beats the disk count
  CHECK(c.kind == CL_CHIP_LOADED && std::strcmp(c.text, "loaded") == 0);
  c = clRowChip(1, false, false, false, true, "A500");   // one dongle: its name
  CHECK(c.kind == CL_CHIP_LOADED && std::strcmp(c.text, "A500") == 0);
  c = clRowChip(1, false, false, false, true, "A very long dongle name");
  CHECK(c.kind == CL_CHIP_LOADED && std::strlen(c.text) <= 9);
  c = clRowChip(1, true, true, false, true, "");     // a split FluxDrive: which drives
  CHECK(c.kind == CL_CHIP_LOADED && std::strcmp(c.text, "DF0") == 0);
  c = clRowChip(1, true, false, true, true, "");
  CHECK(std::strcmp(c.text, "DF1") == 0);
  c = clRowChip(2, true, true, true, true, "");
  CHECK(std::strcmp(c.text, "DF0 DF1") == 0);
  c = clRowChip(2, true, false, false, false, "");   // split but not loaded: back to the disk count
  CHECK(c.kind == CL_CHIP_DISKS);
  c = clRowChip(1, false, false, false, true, nullptr);
  CHECK(std::strcmp(c.text, "loaded") == 0);
}

// ---- game_sort.h: the library sort, the same order as std::stable_sort with the same comparator ---------------------
struct Item { std::string name; int idx; };
static int itemLess(size_t i, size_t j, void* ctx) { auto& v = *(std::vector<Item>*)ctx; return gsNameLess(v[i].name.c_str(), v[i].idx, v[j].name.c_str(), v[j].idx); }
static void itemSwap(size_t i, size_t j, void* ctx) { auto& v = *(std::vector<Item>*)ctx; std::swap(v[i], v[j]); }

static void test_name_less() {
  CHECK(gsNameLess("abc", 5, "ABD", 1));
  CHECK(!gsNameLess("ABD", 1, "abc", 5));
  CHECK(!gsNameLess("abc", 1, "ABC", 1));          // the same name, the same index: not less
  CHECK(gsNameLess("abc", 1, "ABC", 2));           // the same name: file order (index) decides
  CHECK(!gsNameLess("abc", 2, "ABC", 1));
  CHECK(gsNameLess("", 9, "a", 1));
  CHECK(gsNameLess("Zebra", 1, "apple2", 2) == false);   // case-insensitive: apple2 < Zebra
}

static void test_sort_matches_the_old_order() {
  srand(7);
  for (int round = 0; round < 40; round++) {
    const int n = round == 0 ? 0 : (round == 1 ? 1 : (round == 2 ? 2 : rand() % 400));
    std::vector<Item> v;
    static const char* words[] = {"Lemmings", "lemmings", "Zool", "ZOOL", "Turrican", "Alien Breed", "alien breed", "Xenon", "2 Disk Thing", "#hash", "Beast", "beast"};
    for (int i = 0; i < n; i++) {
      std::string s = words[rand() % 12]; if (rand() % 3 == 0) s += " " + std::to_string(rand() % 30);
      v.push_back({s, i});
    }
    std::vector<Item> ref = v;
    std::stable_sort(ref.begin(), ref.end(), [](const Item& a, const Item& b) { return gsNameLess(a.name.c_str(), a.idx, b.name.c_str(), b.idx) != 0; });
    gsHeapSort(v.size(), itemLess, itemSwap, &v);
    bool same = v.size() == ref.size();
    for (size_t i = 0; same && i < v.size(); i++) same = v[i].idx == ref[i].idx;
    CHECK(same);
  }
}

static void test_sort_counts() {
  // a heapsort of n items does O(n log n) work: a million-item guard against a quadratic mistake
  const int n = 20000; std::vector<Item> v; for (int i = 0; i < n; i++) v.push_back({std::to_string((i * 7919) % n), i});
  long cmps = 0;
  struct Ctx { std::vector<Item>* v; long* c; } cx{&v, &cmps};
  gsHeapSort(v.size(), [](size_t i, size_t j, void* c) { auto* x = (Ctx*)c; (*x->c)++; return gsNameLess((*x->v)[i].name.c_str(), (*x->v)[i].idx, (*x->v)[j].name.c_str(), (*x->v)[j].idx); },
             [](size_t i, size_t j, void* c) { auto* x = (Ctx*)c; std::swap((*x->v)[i], (*x->v)[j]); }, &cx);
  CHECK(cmps < (long)n * 40);
  bool sorted = true; for (int i = 1; i < n; i++) if (gsNameLess(v[i].name.c_str(), v[i].idx, v[i - 1].name.c_str(), v[i - 1].idx)) sorted = false;
  CHECK(sorted);
}

static void test_settings_layout() {
  ClSetTabs t = clSettingsTabs(480);
  CHECK(t.back.x == 8 && t.back.w == 48 && t.back.h == 48 && t.back.y == 4);
  CHECK(t.tab[0].x >= cuRight(t.back) && cuRight(t.tab[3]) == 472);
  for (int i = 0; i < 4; i++) { CHECK(t.tab[i].h == 48 && t.tab[i].w >= 96 && t.tab[i].y == 4); if (i) CHECK(t.tab[i].x == cuRight(t.tab[i - 1])); }
  CHECK(clSetTabAt(t, t.tab[2].x + 3, 20) == 2 && clSetTabAt(t, t.tab[0].x, 4) == 0 && clSetTabAt(t, 3, 20) == -1);
  CHECK(clSetTabAt(t, t.tab[1].x + 3, 60) == -1);   // below the tab row
  CHECK(CL_SET_H >= CU_KEY && CL_SET_PITCH == CL_SET_H + 4);
  // rows of 56 at a pitch of 60 in a view from y 60 to 312
  const int top = 60, viewH = 252;
  CHECK(clSetMaxScroll(4, viewH) == 0 && clSetMaxScroll(5, viewH) == 5 * 60 - viewH && clSetMaxScroll(0, viewH) == 0);
  CHECK(clSetRowAt(0, top, 60, 10) == 0 && clSetRowAt(0, top, 60 + 59, 10) == 0 && clSetRowAt(0, top, 120, 10) == 1);
  CHECK(clSetRowAt(0, top, 59, 10) == -1 && clSetRowAt(100, top, 60, 10) == 1 && clSetRowAt(0, top, 300, 3) == -1);
  CuRect r = clSetRowRect(2, 30, top, 8, 464);
  CHECK(r.h == 56 && r.y == top + 2 * 60 - 30 && r.w == 464);
  // the confirmation screen: the message, Cancel on the left, the action on the right, 16 px or more between, 48 high, far from Back
  ClConfirm c = clConfirmLayout(480, 320);
  CHECK(c.back.h == 48 && c.cancel.h == 48 && c.ok.h == 48 && c.cancel.y == c.ok.y);
  CHECK(c.ok.x - cuRight(c.cancel) >= 16 && cuRight(c.ok) <= 472 && c.cancel.x == 8 && cuBottom(c.ok) <= 312);
  CHECK(c.cancel.y >= cuBottom(c.back) + 100 && c.msg.y >= cuBottom(c.back) && cuBottom(c.msg) <= c.cancel.y - 8);
  CHECK(clConfirmTap(c, c.ok.x + 5, c.ok.y + 5) == CL_CF_OK && clConfirmTap(c, c.cancel.x + 5, c.cancel.y + 5) == CL_CF_CANCEL);
  CHECK(clConfirmTap(c, c.back.x + 5, c.back.y + 5) == CL_CF_CANCEL && clConfirmTap(c, 240, c.msg.y + 5) == CL_CF_NONE);
  CHECK(clConfirmTap(c, cuRight(c.cancel) + 4, c.cancel.y + 5) == CL_CF_NONE);   // the gap between the two keys
}

static void test_portrait_lists() {
  // the letter grid on 320: four columns of 64, seven rows; every cell on screen, A..Z and '#'
  CHECK(clLetterCols(480) == 7 && clLetterCols(320) == 4);
  for (int i = 0; i < CL_LETTER_N; i++) { CuRect c = clLetterCell(i, 320, 60); CHECK(c.x >= 8 && cuRight(c) <= 312 && cuBottom(c) <= 480 - 8 && c.w == 64 && c.h == 48); }
  CHECK(clLetterAt(320, 60, clLetterCell(26, 320, 60).x + 5, clLetterCell(26, 320, 60).y + 5) == 26);
  CHECK(clLetterAt(320, 60, clLetterCell(5, 320, 60).x + 5, clLetterCell(5, 320, 60).y + 5) == 5);
  CHECK(clLetterCell(4, 320, 60).y == 60 + 50 && clLetterCell(4, 320, 60).x == clLetterCell(0, 320, 60).x);   // the fifth letter starts the second row
  // Settings tabs on 320: Back on top, the four tabs in a row of their own below it
  ClSetTabs t = clSettingsTabs(320);
  CHECK(t.back.w == 48 && t.back.h == 48);
  for (int i = 0; i < 4; i++) { CHECK(t.tab[i].h == 48 && t.tab[i].w >= 64 && t.tab[i].y >= cuBottom(t.back) && cuRight(t.tab[i]) <= 312); if (i) CHECK(t.tab[i].x == cuRight(t.tab[i - 1])); }
  CHECK(t.listTop >= cuBottom(t.tab[0]) + 4 && clSetTabAt(t, t.tab[3].x + 5, t.tab[3].y + 5) == 3);
  ClSetTabs tl = clSettingsTabs(480);
  CHECK(tl.listTop == 60 && tl.tab[0].y == 4);   // landscape unchanged
  ClConfirm c = clConfirmLayout(320, 480);
  CHECK(c.ok.x - cuRight(c.cancel) >= 16 && cuBottom(c.ok) <= 472 && cuRight(c.ok) <= 312 && c.cancel.w >= 100);
  ClSearchRow r = clSearchRow(CuRect{8, 48, 304, 48});
  CHECK(r.search.w >= 80 && r.star.w == 48 && cuRight(r.search) < r.star.x && cuRight(r.star) < r.az.x && cuRight(r.filter) == 312);   // the search field shrinks, the keys do not
}

static void test_fav_view() {
  const unsigned char fav[8] = {0, 1, 0, 0, 1, 1, 0, 0};
  int out[8];
  CHECK(clViewBuild(fav, 8, false, out, 8) == 8);                    // all: the list as it is (out is not used)
  CHECK(clViewBuild(fav, 8, true, out, 8) == 3 && out[0] == 1 && out[1] == 4 && out[2] == 5);   // favourites only, in list order
  const unsigned char none[4] = {0, 0, 0, 0};
  CHECK(clViewBuild(none, 4, true, out, 8) == 0 && clViewBuild(none, 0, true, out, 8) == 0);
  int small[2]; CHECK(clViewBuild(fav, 8, true, small, 2) == 2);     // never past its room
  clViewBuild(fav, 8, true, out, 8);
  CHECK(clViewGame(out, 3, true, 1) == 4 && clViewGame(out, 3, true, 5) == -1 && clViewGame(out, 8, false, 6) == 6);
  CHECK(clViewPos(out, 3, true, 5, 8) == 2 && clViewPos(out, 3, true, 4, 8) == 1 && clViewPos(out, 3, true, 2, 8) == -1);   // a game that is not a favourite is not in the view
  CHECK(clViewPos(out, 8, false, 6, 8) == 6 && clViewPos(out, 8, false, 9, 8) == -1);
}

int main() {
  test_search_row();
  test_fav_view();
  test_portrait_lists();
  test_settings_layout();
  test_rows();
  test_letter_grid();
  test_split_disk();
  test_row_chip();
  test_name_less();
  test_sort_matches_the_old_order();
  test_sort_counts();
  std::printf("%d checks, %d failed\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
