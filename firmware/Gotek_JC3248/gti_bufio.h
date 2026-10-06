// gti_bufio.h - lab15f: read and write the library's cache files in big blocks.
// ============================================================================
// Warm boot and a library switch read three cache files: .index (one path per line),
// .gamecache (one game per line) and .nfocache (binary, three small fields per game).
// They were read with readStringUntil('\n') / tiny f.read() calls: every CHARACTER (or
// field) went through the whole file stack separately, and every line became a String.
// On the 20,000-game test card that was 13.8 s for .index and 15.4 s for .gamecache +
// .nfocache (gti.log 27 Sep).
//
// BufRd pulls the file in 16 KB blocks and hands out lines (trimmed exactly like
// String::trim()) or raw bytes from memory. BufWr collects output and writes it in
// 16 KB blocks. The file formats do not change - byte for byte the same files.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <FS.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#ifdef ARDUINO
#include "esp_heap_caps.h"
static inline void* bio_alloc(size_t n){   // internal RAM first (the SD driver DMAs straight into it), PSRAM if that is short
  void* p=heap_caps_malloc(n,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  return p?p:heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
#else
static inline void* bio_alloc(size_t n){ return malloc(n); }
#endif

class BufRd{
public:
  explicit BufRd(fs::File& f,size_t cap=16384):_f(f),_cap(cap){ _b=(uint8_t*)bio_alloc(_cap); if(!_b){ _cap=512; _b=(uint8_t*)malloc(_cap); } }
  ~BufRd(){ free(_b); }
  bool ok() const { return _b!=nullptr; }
  int getc(){ if(_pos>=_len && !fill()) return -1; return _b[_pos++]; }
  size_t read(uint8_t* d,size_t n){
    size_t got=0;
    while(got<n){ if(_pos>=_len && !fill()) break; size_t c=n-got; if(c>_len-_pos) c=_len-_pos; memcpy(d+got,_b+_pos,c); _pos+=c; got+=c; }
    return got;
  }
  // Next line without its '\n', trimmed of leading/trailing whitespace (as String::trim()). false = end of file.
  // A line longer than max-1 sets `cut` (the rest of that line is skipped).
  bool line(char* out,size_t max,size_t& n,bool& cut){
    n=0; cut=false; int c; bool any=false;
    while((c=getc())>=0){ any=true; if(c=='\n') break; if(n<max-1) out[n++]=(char)c; else cut=true; }
    if(!any) return false;
    out[n]=0;
    size_t s=0; while(s<n && isspace((unsigned char)out[s])) s++;
    while(n>s && isspace((unsigned char)out[n-1])) n--;
    if(s){ memmove(out,out+s,n-s); n-=s; }
    out[n]=0; return true;
  }
private:
  bool fill(){ if(_eof||!_b) return false; int r=_f.read(_b,_cap); if(r<=0){ _eof=true; _pos=_len=0; return false; } _len=(size_t)r; _pos=0; return true; }
  fs::File& _f; uint8_t* _b=nullptr; size_t _cap,_pos=0,_len=0; bool _eof=false;
};

class BufWr{
public:
  explicit BufWr(fs::File& f,size_t cap=16384):_f(f),_cap(cap){ _b=(uint8_t*)bio_alloc(_cap); if(!_b){ _cap=512; _b=(uint8_t*)malloc(_cap); } }
  ~BufWr(){ flush(); free(_b); }
  bool ok() const { return _b!=nullptr && !_err; }
  void write(const void* p,size_t n){
    const uint8_t* s=(const uint8_t*)p;
    if(!_b){ if(_f.write(s,n)!=n) _err=true; return; }
    while(n){ size_t c=_cap-_n; if(c>n) c=n; memcpy(_b+_n,s,c); _n+=c; s+=c; n-=c; if(_n==_cap) flush(); }
  }
  void str(const char* s){ write(s,strlen(s)); }
  void num(long v){ char t[24]; int k=snprintf(t,sizeof t,"%ld",v); write(t,(size_t)k); }
  void nl(){ write("\r\n",2); }                     // what Print::println() writes
  void flush(){ if(_b&&_n){ if(_f.write(_b,_n)!=_n) _err=true; _n=0; } }
private:
  fs::File& _f; uint8_t* _b=nullptr; size_t _cap,_n=0; bool _err=false;
};
