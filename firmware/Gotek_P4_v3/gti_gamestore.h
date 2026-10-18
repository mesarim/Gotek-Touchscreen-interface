// gti_gamestore.h - lab15b: one game's record, compact.
// ============================================================================
// Before lab15b every game in g_games was an 80-byte GameEntry holding three Strings
// (name, cover path, blurb) and a std::vector<int> of its disks. Every one of those
// that wasn't tiny had its OWN PSRAM heap block with the allocator's header on top:
// measured on Mez's card, 11,602 games took 2,057 KB (~177 B a game).
//
// Now a GameEntry is 36 bytes and holds no heap blocks of its own:
//   - name, cover path and blurb are AStr: one pointer to text in the GAME ARENA
//     (32 KB PSRAM blocks the text is packed into, back to back, no headers);
//   - the disk list is a DiskList: a single-disk game keeps its one index inside the
//     record (no allocation at all); a multi-disk set points at its indices in the arena.
// Everything in the arena belongs to g_games and is freed in one go with it
// (gamesClear()). Text is never edited in place: giving a field a new value adds the
// new text and moves the pointer (setting the SAME value again adds nothing). The only
// runtime additions are one cover path / title / blurb per game, found lazily when a
// game is first shown - bounded by the number of games.
//
// AStr mimics the String calls the firmware makes on these fields: c_str(), length(),
// charAt(), [], ==/!= against String or text, = from String or text, and it turns into
// a String wherever one is expected (a COPY - never a reference into the arena).
// "" is stored as no pointer at all and "?" (no cover) as a shared constant, so neither
// costs arena space.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <vector>
#ifdef ARDUINO
#include "esp_heap_caps.h"
static inline void* ga_alloc(size_t n){ return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); }   // PSRAM only (same rule as PathList)
#else
static inline void* ga_alloc(size_t n){ return malloc(n); }
#endif

class GameArena{
public:
  static constexpr size_t CHUNK=32*1024;
  // n bytes aligned to `align` (a power of 2). nullptr when PSRAM is full - never throws.
  void* alloc(size_t n,size_t align=1){
    if(!n) return nullptr;
    if(n>CHUNK/2){                                    // big item: a block of its own, current block untouched
      void* b=ga_alloc(n); if(!b){ _failed++; return nullptr; }
      _blocks.push_back(b); _bytes+=n; _used+=n; return b;
    }
    size_t off=(_off+align-1)&~(align-1);
    if(!_cur || off+n>CHUNK){
      void* b=ga_alloc(CHUNK); if(!b){ _failed++; return nullptr; }
      _blocks.push_back(b); _cur=(uint8_t*)b; _bytes+=CHUNK; off=0;
    }
    _off=off+n; _used+=n; return _cur+off;
  }
  void release(){ for(void* b:_blocks) free(b); std::vector<void*>().swap(_blocks); _cur=nullptr; _off=0; _bytes=_used=0; _failed=0; }
  size_t bytes()  const { return _bytes; }            // PSRAM held
  size_t used()   const { return _used; }             // of which filled
  size_t blocks() const { return _blocks.size(); }
  uint32_t failed() const { return _failed; }         // allocations refused (PSRAM full) since the last release()
  void clearFailed(){ _failed=0; }
private:
  std::vector<void*> _blocks; uint8_t* _cur=nullptr; size_t _off=0,_bytes=0,_used=0; uint32_t _failed=0;
};
static GameArena g_gtext;                              // the game arena (g_games' text + disk lists)
static const char g_astr_q[2]="?";

class AStr{
public:
  const char* c_str() const { return _p?_p:""; }
  unsigned length() const { return _p?(unsigned)strlen(_p):0u; }
  bool isEmpty() const { return !_p||!*_p; }
  char charAt(unsigned i) const { return i<length()?_p[i]:0; }
  char operator[](unsigned i) const { return charAt(i); }
  operator String() const { return String(c_str()); }
  bool operator==(const char* s) const { return strcmp(c_str(),s?s:"")==0; }
  bool operator==(const String& s) const { return strcmp(c_str(),s.c_str())==0; }
  bool operator!=(const char* s) const { return !(*this==s); }
  bool operator!=(const String& s) const { return !(*this==s); }
  AStr& operator=(const char* s){ set(s,s?strlen(s):0); return *this; }
  AStr& operator=(const String& s){ set(s.c_str(),strlen(s.c_str())); return *this; }
  // false = PSRAM full: the field is left EMPTY ("not known yet"), never half-written
  bool set(const char* s,size_t n){
    if(!n){ _p=nullptr; return true; }
    if(n==1&&s[0]=='?'){ _p=g_astr_q; return true; }
    if(_p && strncmp(_p,s,n)==0 && _p[n]==0) return true;   // same text again: nothing added
    char* d=(char*)g_gtext.alloc(n+1,1); if(!d){ _p=nullptr; return false; }
    memcpy(d,s,n); d[n]=0; _p=d; return true;
  }
private:
  const char* _p=nullptr;
};

class DiskList{
public:
  DiskList(){ _u.one=0; }
  size_t size() const { return _n; }
  bool empty() const { return _n==0; }
  int operator[](size_t i) const { return _n<=1?(int)_u.one:(int)_u.many[i]; }
  void setOne(int v){ _u.one=v; _n=1; }
  // k indices in order. false = PSRAM full: keeps only the first disk.
  bool set(const int* v,size_t k){
    if(k==0){ _n=0; return true; }
    if(k==1){ setOne(v[0]); return true; }
    if(k>65535) k=65535;
    int32_t* d=(int32_t*)g_gtext.alloc(k*sizeof(int32_t),4); if(!d){ setOne(v[0]); return false; }
    for(size_t i=0;i<k;i++) d[i]=v[i];
    _u.many=d; _n=(uint16_t)k; return true;
  }
private:
  union{ int32_t one; int32_t* many; } _u; uint16_t _n=0;
};
