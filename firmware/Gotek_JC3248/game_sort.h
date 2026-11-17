// game_sort.h - the library's sort order and the sort itself (A600-calm2/3).
//
// The list is a std::deque<GameEntry>. std::sort over it pulls in four deque-iterator template instantiations whose literal
// pools the Xtensa linker could not reach once the sketch grew a few KB ("dangerous relocation: l32r: literal target out of
// range"), so the sketch sorts with gsHeapSort: a plain heapsort over two callbacks (compare, swap), no templates.
//
// The order: names compared without regard to case (strcasecmp), equal names in file order (the entry's first file index).
// std::sort left equal names in an unspecified order (stable only for tiny lists); this makes it the same every time. For
// different names the order is exactly the old one. Pure, no Arduino types: tools/test_calm_list.sh checks it against
// std::stable_sort with the same comparator.
#pragma once
#include <stddef.h>
#include <string.h>
#include <strings.h>

// "a sorts before b": nonzero when it does.
inline int gsNameLess(const char* a, int ai, const char* b, int bi) {
  const int c = strcasecmp(a, b);
  return c != 0 ? c < 0 : ai < bi;
}

typedef int  (*GsLess)(size_t i, size_t j, void* ctx);   // item i sorts before item j
typedef void (*GsSwap)(size_t i, size_t j, void* ctx);

inline void gsSift(size_t root, size_t end, GsLess less, GsSwap swap, void* ctx) {
  for (;;) {
    size_t c = 2 * root + 1;
    if (c >= end) break;
    if (c + 1 < end && less(c, c + 1, ctx)) c++;
    if (!less(root, c, ctx)) break;
    swap(root, c, ctx); root = c;
  }
}
inline void gsHeapSort(size_t n, GsLess less, GsSwap swap, void* ctx) {
  if (n < 2) return;
  for (size_t i = n / 2; i-- > 0;) gsSift(i, n, less, swap, ctx);
  for (size_t e = n; e-- > 1;) { swap(0, e, ctx); gsSift(0, e, less, swap, ctx); }
}
