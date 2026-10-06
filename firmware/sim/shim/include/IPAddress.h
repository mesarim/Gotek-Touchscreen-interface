#pragma once
#include <Arduino.h>
class IPAddress : public Printable {
  uint8_t a[4] = {0,0,0,0};
public:
  IPAddress() {}
  IPAddress(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) { a[0]=b0;a[1]=b1;a[2]=b2;a[3]=b3; }
  String toString() const { char s[16]; snprintf(s, 16, "%u.%u.%u.%u", a[0],a[1],a[2],a[3]); return String(s); }
  uint8_t operator[](int i) const { return a[i]; }
  operator uint32_t() const { return a[0]|(a[1]<<8)|(a[2]<<16)|((uint32_t)a[3]<<24); }
  size_t printTo(Print &p) const override { return p.print(toString()); }
};
