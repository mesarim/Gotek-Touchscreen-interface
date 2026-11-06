// The skin's colours as pure logic (no Arduino, no ESP-IDF): the firmware and the host tests compile it.
//
// A device keeps only the eleven colour tokens of its skin (docs/house-style.md section 2) in /SKIN.JSON; the app reads
// them from GET /api/skin and sets its CSS variables. NEO is the default. from_json takes `{"bg":"#081021",...}` (the
// token names as the CSS has them, without "--"), and every token that is missing or not a 6-digit hex colour keeps
// NEO's value, so a broken or half skin never leaves the page unreadable.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <cstring>

namespace skin {

enum Tok { Bg, Bar, Card, Line, Txt, Dim, Accent, Ok, Warn, Ins, Danger, kCount };

struct Tokens {
    uint32_t c[kCount];
};

inline const char* name(int t) {
    static const char* const n[kCount] = {"bg", "bar", "card", "line", "txt", "dim", "accent", "ok", "warn", "ins", "danger"};
    return n[t];
}

inline const Tokens& neo() {
    static const Tokens t = {{0x081021, 0x080C19, 0x101831, 0x42598C, 0xEFF3FF, 0x94A2BD, 0xF7B231, 0x52D27B, 0xF78E31,
                              0x7BD2F7, 0xFF6B6B}};
    return t;
}

// "#RRGGBB" or "RRGGBB", nothing more.
inline bool parse_hex(const char* s, uint32_t* rgb) {
    if (!s) return false;
    if (*s == '#') ++s;
    uint32_t v = 0;
    for (int i = 0; i < 6; ++i) {
        const char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = v << 4 | static_cast<uint32_t>(d);
    }
    if (s[6] != '\0' && s[6] != '"') return false;
    *rgb = v;
    return true;
}

// The value after `"<key>":"` in json, read up to the next quote (the skin JSON has flat string values only).
inline bool find_value(const char* json, const char* key, char* out, size_t n) {
    char pat[24];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char* p = std::strstr(json, pat);
    if (!p) return false;
    p += std::strlen(pat);
    size_t i = 0;
    while (p[i] && p[i] != '"' && i + 1 < n) { out[i] = p[i]; ++i; }
    if (p[i] != '"') return false;
    out[i] = '\0';
    return true;
}

inline Tokens from_json(const char* json) {
    Tokens t = neo();
    if (!json) return t;
    for (int k = 0; k < kCount; ++k) {
        char v[16];
        uint32_t rgb;
        if (find_value(json, name(k), v, sizeof v) && parse_hex(v, &rgb)) t.c[k] = rgb;
    }
    return t;
}

// {"bg":"#081021",...} with all eleven; the length it needs (without the NUL) when out is nullptr.
inline size_t to_json(const Tokens& t, char* out, size_t n) {
    char buf[320];
    size_t len = 0;
    buf[len++] = '{';
    for (int k = 0; k < kCount; ++k)
        len += static_cast<size_t>(snprintf(buf + len, sizeof buf - len, "%s\"%s\":\"#%06X\"", k ? "," : "", name(k),
                                            static_cast<unsigned>(t.c[k] & 0xFFFFFF)));
    buf[len++] = '}';
    buf[len] = '\0';
    if (out && n) {
        const size_t m = len < n - 1 ? len : n - 1;
        std::memcpy(out, buf, m);
        out[m] = '\0';
    }
    return len;
}

}  // namespace skin
