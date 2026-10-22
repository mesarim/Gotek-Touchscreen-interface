// gti_pathlist.h - lab15a: the disk-image list, compact.
// ============================================================================
// Before lab15 the library's image list was a std::vector<String>: every image paid
// for a 16-byte String object PLUS its own heap block holding the WHOLE path, with
// the allocator's header on top - about 100 bytes per image on a TOSEC card
// (measured on Mez's 25,800-image .index: 2.5 MB).
//
// PathList stores:
//   - each FOLDER path once;
//   - each FILE name as a record [shared-prefix length][rest of the name], packed
//     back-to-back in 32 KB PSRAM blocks. After compact() (called once the list is
//     sorted) a name only stores what differs from the name before it - TOSEC sets
//     ("... (Disk 1 of 3)", "... (Disk 2 of 3)", "[a]", "[cr]" variants) share most
//     of their text. Every 16th name is stored whole, so rebuilding any name reads
//     at most 16 records.
//   - 8 bytes per image: where its record starts, which folder.
// On that same .index: ~0.9 MB instead of 2.5 MB. No per-image heap blocks, so PSRAM
// is no longer cut into tens of thousands of fragments either.
//
// It mimics the parts of std::vector<String> the firmware used: size(), empty(),
// operator[] (the full path as a NEW String - by value, never a reference),
// push_back(full path), reserve/capacity, clear(), resize() (shrink only),
// shrink_to_fit(), sort() (same order as std::sort over the Strings) and release()
// (the old "std::vector<String>().swap(v)" free idiom). operator[] rebuilds exactly
// the string that was pushed.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#ifdef ARDUINO
#include "esp_heap_caps.h"
// lab15a2: PSRAM ONLY. 15a fell back to internal RAM when PSRAM was full, so on a big card the
// list (or its packing copy) could eat the ~200 KB of internal RAM the SD driver, radio and UI
// live on. Now a full PSRAM just means "no room" - the scan's memory check handles that.
static inline void* pl_alloc(size_t n){ return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); }
static inline void* pl_realloc(void* o,size_t n){ return heap_caps_realloc(o,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); }
#else
static inline void* pl_alloc(size_t n){ return malloc(n); }
static inline void* pl_realloc(void* o,size_t n){ return realloc(o,n); }
#endif

class PathList {
public:
  static const uint32_t CHUNK_BITS = 15;                 // 32 KB text blocks
  static const uint32_t CHUNK = 1u << CHUNK_BITS;
  static const uint32_t RESTART = 16;                    // every 16th name is stored whole
  PathList() {}
  ~PathList() { release(); }
  PathList(const PathList&) = delete;
  PathList& operator=(const PathList&) = delete;

  size_t size() const { return n_; }
  bool   empty() const { return n_ == 0; }
  size_t capacity() const { return ecap_; }
  bool   compacted() const { return packed_; }

  // The full path, rebuilt: folder + "/" + name (folder "" = a file at the card root).
  String operator[](size_t i) const {
    char nm[260]; nameInto(i, nm);
    const char* d = dir(i);
    String s; s.reserve(strlen(d) + 1 + strlen(nm));
    s += d; s += '/'; s += nm; return s;
  }
  String nameStr(size_t i) const { char nm[260]; nameInto(i, nm); return String(nm); }
  const char* dir(size_t i) const { return txt_.at(dirs_[ent_[i].dir]); }

  void reserve(size_t k) { if (k > ecap_) growEnt(k); }

  // Bytes the NEXT push_back may need in one PSRAM block (for the scan's memory check).
  size_t growBytes() const {
    size_t need = CHUNK;
    if (n_ + 1 >= ecap_) need = std::max(need, (size_t)(ecap_ ? ecap_ * 2 : 1024) * sizeof(Ent));
    return need;
  }
  // PSRAM held right now (logs / capacity report).
  size_t bytes() const { return ecap_ * sizeof(Ent) + dcap_ * sizeof(uint32_t) + txt_.bytes(); }

  bool push_back(const String& full) { return push_back(full.c_str()); }
  bool push_back(const char* full) {
    const char* sl = strrchr(full, '/');
    size_t dl = sl ? (size_t)(sl - full) : 0;
    const char* nm = sl ? sl + 1 : full;
    size_t nl = strlen(nm); if (nl > 255) return false;   // FAT long names are <= 255
    int di = findDir(full, dl);
    if (di < 0) {
      uint32_t off; if (!txt_.put(full, dl, &off)) return false;
      if (dn_ >= dcap_) { size_t nc = dcap_ ? dcap_ * 2 : 256; uint32_t* p = (uint32_t*)pl_realloc(dirs_, nc * sizeof(uint32_t)); if (!p) return false; dirs_ = p; dcap_ = nc; }
      di = (int)dn_; dirs_[dn_++] = off;
      recent_[recentPos_++ & 3] = di;
    }
    if (n_ >= ecap_ && !growEnt(ecap_ ? ecap_ * 2 : 1024)) return false;
    char rec[258]; rec[0] = 0; memcpy(rec + 1, nm, nl);    // a whole name: shared prefix 0
    uint32_t noff; if (!txt_.put(rec, nl + 1, &noff)) return false;
    ent_[n_].name = noff; ent_[n_].dir = (uint32_t)di; n_++;
    sorted_ = false;
    return true;
  }

  // Shrink only (all the firmware ever asks of resize). Records before k are untouched,
  // and every name still decodes from a whole name at or before it.
  void resize(size_t k) { if (k < n_) n_ = k; }
  void clear() { n_ = 0; dn_ = 0; txt_.freeAll(); recentPos_ = 0; for (int i = 0; i < 4; i++) recent_[i] = -1; packed_ = false; sorted_ = false; }
  void release() {
    clear();
    free(ent_); ent_ = nullptr; ecap_ = 0;
    free(dirs_); dirs_ = nullptr; dcap_ = 0;
    txt_.releaseIndex();
  }
  void shrink_to_fit() {
    if (n_ && n_ < ecap_) { Ent* p = (Ent*)pl_realloc(ent_, n_ * sizeof(Ent)); if (p) { ent_ = p; ecap_ = n_; } }
    if (dn_ && dn_ < dcap_) { uint32_t* p = (uint32_t*)pl_realloc(dirs_, dn_ * sizeof(uint32_t)); if (p) { dirs_ = p; dcap_ = dn_; } }
  }
  // Same order as std::sort over the full-path Strings (byte-wise, unsigned - String's operator<).
  // A compacted list is already sorted (compact() runs on a sorted list) and must not be reordered.
  void sort() {
    if (packed_ || sorted_) return;
    std::sort(ent_, ent_ + n_, [this](const Ent& a, const Ent& b) { return cmp(a, b) < 0; });
    sorted_ = true;
  }
  // Rewrite every name as [prefix shared with the previous name][the rest], a whole name every
  // RESTART entries. Needs room for the new text next to the old for a moment (the new text is
  // ~40% of the old); if that isn't there it leaves the list as it is and returns false.
  bool compact() {
    if (packed_ || n_ == 0) { packed_ = true; return true; }
    Arena nt;
    // Folders are re-listed in entry order, one copy per run of equal folders (the sorted list keeps a
    // folder's files together), so duplicates the walk made (a folder revisited after its sub-folders)
    // collapse too.
    size_t ndc = 256, ndn = 0; uint32_t* nd = (uint32_t*)pl_alloc(ndc * sizeof(uint32_t)); if (!nd) return false;
    uint32_t* no = (uint32_t*)pl_alloc(n_ * 2 * sizeof(uint32_t)); if (!no) { free(nd); return false; }   // new name offset + new folder, per entry
    char prev[260] = {0}; size_t prevLen = 0; const char* lastDir = nullptr;
    bool ok = true;
    for (size_t i = 0; ok && i < n_; i++) {
      const char* d = txt_.at(dirs_[ent_[i].dir]);
      if (!lastDir || strcmp(lastDir, d) != 0) {
        if (ndn >= ndc) { size_t nc = ndc * 2; uint32_t* q = (uint32_t*)pl_realloc(nd, nc * sizeof(uint32_t)); if (!q) { ok = false; break; } nd = q; ndc = nc; }
        if (!nt.put(d, strlen(d), &nd[ndn])) { ok = false; break; }
        ndn++; lastDir = d;
      }
      no[2 * i + 1] = (uint32_t)(ndn - 1);
      const char* cur = txt_.at(ent_[i].name) + 1;            // raw records are whole names
      size_t cl = strlen(cur);
      size_t p = 0;
      if (i % RESTART) { size_t m = std::min(cl, prevLen); if (m > 255) m = 255; while (p < m && prev[p] == cur[p]) p++; }
      char rec[258]; rec[0] = (char)(uint8_t)p; memcpy(rec + 1, cur + p, cl - p);
      if (!nt.put(rec, 1 + cl - p, &no[2 * i])) { ok = false; break; }
      memcpy(prev, cur, cl + 1); prevLen = cl;
    }
    if (!ok) { nt.freeAll(); nt.releaseIndex(); free(nd); free(no); return false; }
    for (size_t i = 0; i < n_; i++) { ent_[i].name = no[2 * i]; ent_[i].dir = no[2 * i + 1]; }
    free(no);
    free(dirs_); dirs_ = nd; dn_ = ndn; dcap_ = ndc;
    txt_.freeAll(); txt_.releaseIndex(); txt_ = nt;             // the old text goes back to PSRAM in one go
    for (int k = 0; k < 4; k++) recent_[k] = -1;
    packed_ = true; sorted_ = true;
    return true;
  }

private:
  struct Ent { uint32_t name; uint32_t dir; };
  struct Arena {
    char** chunks = nullptr; size_t nchunks = 0, ccap = 0; size_t used = CHUNK;
    const char* at(uint32_t off) const { return chunks[off >> CHUNK_BITS] + (off & (CHUNK - 1)); }
    size_t bytes() const { return nchunks * CHUNK + ccap * sizeof(char*); }
    bool put(const char* s, size_t len, uint32_t* off) {         // len bytes + a terminating 0
      if (len + 1 > CHUNK) return false;
      if (used + len + 1 > CHUNK) {
        if (nchunks >= ccap) { size_t nc = ccap ? ccap * 2 : 64; char** p = (char**)pl_realloc(chunks, nc * sizeof(char*)); if (!p) return false; chunks = p; ccap = nc; }
        char* c = (char*)pl_alloc(CHUNK); if (!c) return false;
        chunks[nchunks++] = c; used = 0;
      }
      char* dst = chunks[nchunks - 1] + used;
      memcpy(dst, s, len); dst[len] = 0;
      *off = (uint32_t)(((nchunks - 1) << CHUNK_BITS) | used);
      used += len + 1;
      return true;
    }
    void freeAll() { for (size_t c = 0; c < nchunks; c++) free(chunks[c]); nchunks = 0; used = CHUNK; }
    void releaseIndex() { free(chunks); chunks = nullptr; ccap = 0; }
  };
  Ent*      ent_ = nullptr;  size_t n_ = 0,  ecap_ = 0;
  uint32_t* dirs_ = nullptr; size_t dn_ = 0, dcap_ = 0;
  Arena     txt_;
  bool      packed_ = false, sorted_ = false;
  int       recent_[4] = {-1, -1, -1, -1}; unsigned recentPos_ = 0;

  bool growEnt(size_t k) { Ent* p = (Ent*)pl_realloc(ent_, k * sizeof(Ent)); if (!p) return false; ent_ = p; ecap_ = k; return true; }
  // Name i into out (>= 256 bytes): back to the nearest whole name, then forward.
  void nameInto(size_t i, char* out) const {
    size_t j = i;
    while (j > 0 && (uint8_t)txt_.at(ent_[j].name)[0] != 0) j--;
    strcpy(out, txt_.at(ent_[j].name) + 1);
    for (size_t k = j + 1; k <= i; k++) { const char* r = txt_.at(ent_[k].name); strcpy(out + (uint8_t)r[0], r + 1); }
  }
  // Folders arrive in walk order: the one we want is nearly always one of the last few.
  int findDir(const char* d, size_t dl) const {
    for (int k = 0; k < 4; k++) { int di = recent_[k]; if (di < 0) continue;
      const char* s = txt_.at(dirs_[di]); if (strlen(s) == dl && memcmp(s, d, dl) == 0) return di; }
    return -1;
  }
  // Compare two RAW entries as if they were the strings  dir + "/" + name.
  int cmp(const Ent& a, const Ent& b) const {
    const unsigned char* pa = (const unsigned char*)txt_.at(dirs_[a.dir]); const unsigned char* pb = (const unsigned char*)txt_.at(dirs_[b.dir]);
    const unsigned char* na = (const unsigned char*)txt_.at(a.name) + 1; const unsigned char* nb = (const unsigned char*)txt_.at(b.name) + 1;
    int sa = 0, sb = 0;                    // 0 = in the folder, 1 = the '/', 2 = in the name
    for (;;) {
      unsigned ca, cb;
      if (sa == 0) { if (*pa) ca = *pa; else { sa = 1; ca = '/'; } } else if (sa == 1) { ca = '/'; } else ca = *na;
      if (sb == 0) { if (*pb) cb = *pb; else { sb = 1; cb = '/'; } } else if (sb == 1) { cb = '/'; } else cb = *nb;
      if (ca != cb) return (int)ca - (int)cb;
      if (ca == 0) return 0;
      if (sa == 0) pa++; else if (sa == 1) sa = 2; else na++;
      if (sb == 0) pb++; else if (sb == 1) sb = 2; else nb++;
    }
  }
};
