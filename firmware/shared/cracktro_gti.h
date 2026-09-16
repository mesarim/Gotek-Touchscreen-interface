#pragma once
// .gti cracktro files. Deliberately free of Arduino, SD and gfx_* so this
// parser compiles on the host for tests and later under Emscripten for the
// web builder's preview. See docs/ideas/cracktro-gti-design.md
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace gti {

constexpr size_t MAX_FILE      = 32u * 1024u;
constexpr int    MAX_PATTERNS  = 4;
constexpr int    MAX_LOGOS     = 2;
constexpr int    LOGO_MAX_W    = 128;
constexpr int    LOGO_MAX_H    = 48;
constexpr size_t MAX_SCROLL    = 512;
constexpr size_t MAX_TEXT      = 64;
constexpr size_t MAX_NAME      = 32;
constexpr size_t LOGO_BYTES    = (LOGO_MAX_W / 8) * LOGO_MAX_H;   // 768

enum Err {
  OK = 0, ERR_EMPTY, ERR_MAGIC, ERR_TOO_BIG, ERR_NO_PATTERNS,
  ERR_TOO_MANY_PATTERNS, ERR_TOO_MANY_LOGOS, ERR_LOGO_SIZE, ERR_BAD_HEX
};

struct Pattern {
  char     fx[16];
  uint32_t timeMs;
  char     title[MAX_TEXT];
  char     sub[MAX_TEXT];
  uint16_t col;
  int8_t   logo;      // index into File::logos, -1 = none
  bool     scroll;
};

struct Logo {
  int     w, h;
  size_t  len;                    // bytes actually used in bits[]
  uint8_t bits[LOGO_BYTES];
};

struct File {
  char    name[MAX_NAME];
  char    author[MAX_NAME];
  Pattern patterns[MAX_PATTERNS];
  int     patternCount;
  Logo    logos[MAX_LOGOS];
  int     logoCount;
  char    scroll[MAX_SCROLL];
};

// Matches CRK_RGB in the sketch exactly. The web preview must use this too,
// or a colour picked in the builder lands slightly off on glass.
inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

inline const char* errText(Err e) {
  switch (e) {
    case OK:                    return "ok";
    case ERR_EMPTY:             return "empty file";
    case ERR_MAGIC:             return "missing GTICRACK magic";
    case ERR_TOO_BIG:           return "file too big";
    case ERR_NO_PATTERNS:       return "no [PATTERN] blocks";
    case ERR_TOO_MANY_PATTERNS: return "too many patterns";
    case ERR_TOO_MANY_LOGOS:    return "too many logos";
    case ERR_LOGO_SIZE:         return "logo too large";
    case ERR_BAD_HEX:           return "bad hex in logo";
  }
  return "unknown";
}

namespace detail {

// One line of the buffer, trimmed, without its terminator. Returns false at end.
struct Lines {
  const char* p; const char* end;
  Lines(const char* buf, size_t len) : p(buf), end(buf + len) {}
  bool next(char* out, size_t cap) {
    if (p >= end) return false;
    const char* nl = p;
    while (nl < end && *nl != '\n' && *nl != '\r') ++nl;
    const char* a = p; const char* b = nl;
    while (a < b && (*a == ' ' || *a == '\t')) ++a;
    while (b > a && (b[-1] == ' ' || b[-1] == '\t')) --b;
    size_t n = (size_t)(b - a);
    if (n >= cap) n = cap - 1;
    memcpy(out, a, n); out[n] = 0;
    p = nl;
    while (p < end && (*p == '\n' || *p == '\r')) ++p;
    return true;
  }
};

inline bool isSkippable(const char* line) {
  return line[0] == 0 || line[0] == ';';
}

} // namespace detail

inline Err parse(const char* buf, size_t len, File& out) {
  memset(&out, 0, sizeof(out));
  for (int i = 0; i < MAX_PATTERNS; ++i) out.patterns[i].logo = -1;
  if (buf == nullptr || len == 0) return ERR_EMPTY;
  if (len > MAX_FILE)             return ERR_TOO_BIG;

  detail::Lines lines(buf, len);
  char line[256];

  bool haveMagic = false;
  while (lines.next(line, sizeof(line))) {
    if (detail::isSkippable(line)) continue;
    haveMagic = (strncmp(line, "GTICRACK 1", 10) == 0);
    break;
  }
  if (!haveMagic) return ERR_MAGIC;

  if (out.patternCount == 0) return ERR_NO_PATTERNS;
  return OK;
}

} // namespace gti
