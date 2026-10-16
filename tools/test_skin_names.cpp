#include "skin_names.h"
#include <cstdio>
#include <cstring>

static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { \
  ++g_run; \
  if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void test_validName() {
  CHECK(skin::validName("NAVY2"));
  CHECK(skin::validName("A_B_9"));
  CHECK(skin::validName("ABCDEFGHIJKLMNOP"));     // 16
  CHECK(!skin::validName("ABCDEFGHIJKLMNOPQ"));   // 17
  CHECK(!skin::validName(""));
  CHECK(!skin::validName(nullptr));
  CHECK(!skin::validName("lower"));
  CHECK(!skin::validName("Mixed"));
  CHECK(!skin::validName("A B"));
  CHECK(!skin::validName("../X"));
  CHECK(!skin::validName("X.GTS"));
  CHECK(!skin::validName("9LIVES"));              // THEME=9 is a number
}

static void test_merge_dedupe_sorted() {
  const char* j[] = {"ZED", "X", "lower", "../X"};
  const char* g[] = {"X", "ALPHA", "ABCDEFGHIJKLMNOPQ", "ABCDEFGHIJKLMNOP"};
  char out[8][17];
  int n = skin::mergeNames(j, 4, g, 4, out, 8);
  CHECK(n == 4);
  CHECK(n == 4 && !strcmp(out[0], "ABCDEFGHIJKLMNOP"));
  CHECK(n == 4 && !strcmp(out[1], "ALPHA"));
  CHECK(n == 4 && !strcmp(out[2], "X"));
  CHECK(n == 4 && !strcmp(out[3], "ZED"));
}

static void test_merge_dupes_within_one_list() {
  const char* j[] = {"B", "B", "A"};
  char out[8][17];
  int n = skin::mergeNames(j, 3, nullptr, 0, out, 8);
  CHECK(n == 2);
  CHECK(n == 2 && !strcmp(out[0], "A") && !strcmp(out[1], "B"));
}

static void test_merge_cap() {
  const char* j[] = {"I", "H", "G", "F", "E"};
  const char* g[] = {"D", "C", "B", "A"};
  char out[8][17];
  int n = skin::mergeNames(j, 5, g, 4, out, 8);
  CHECK(n == 8);
  CHECK(n == 8 && !strcmp(out[0], "A") && !strcmp(out[7], "H"));   // the first 8 in order, I dropped
}

static void test_merge_empty() {
  char out[8][17];
  CHECK(skin::mergeNames(nullptr, 0, nullptr, 0, out, 8) == 0);
  const char* j[] = {"bad", ""};
  CHECK(skin::mergeNames(j, 2, nullptr, 0, out, 8) == 0);
  const char* g[] = {"A"};
  CHECK(skin::mergeNames(g, 1, nullptr, 0, out, 0) == 0);
}

static void test_manifestLen() {
  const uint8_t ok[8] = {'G', 'T', 'S', '1', 0, 1, 0x34, 0x01};   // 0x134 = 308
  CHECK(skin::manifestLen(ok, 8) == 308);
  CHECK(skin::manifestLen(ok, 7) == -1);
  const uint8_t magic[8] = {'G', 'T', 'S', '2', 0, 1, 4, 0};
  CHECK(skin::manifestLen(magic, 8) == -1);
  const uint8_t big[8] = {'G', 'T', 'S', '1', 0, 1, 0x01, 0x04};  // 1025 > MAX_MANIFEST
  CHECK(skin::manifestLen(big, 8) == -1);
  const uint8_t max[8] = {'G', 'T', 'S', '1', 0, 1, 0x00, 0x04};  // 1024
  CHECK(skin::manifestLen(max, 8) == 1024);
}

int main() {
  test_validName();
  test_merge_dedupe_sorted();
  test_merge_dupes_within_one_list();
  test_merge_cap();
  test_merge_empty();
  test_manifestLen();
  std::printf("%d checks, %d failed\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
