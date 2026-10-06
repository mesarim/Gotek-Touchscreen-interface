#pragma once
#include <Arduino.h>
// sim: the only I2C device the GTi talks to is the AXS15231B touch controller (0x3B).
class TwoWire : public Stream {
  uint8_t _addr = 0; uint8_t _rx[16]; int _rxn = 0, _rxi = 0;
public:
  bool begin(int sda = -1, int scl = -1, uint32_t freq = 0) { (void)sda; (void)scl; (void)freq; return true; }
  void beginTransmission(uint8_t a) { _addr = a; }
  void beginTransmission(int a) { _addr = (uint8_t)a; }
  uint8_t endTransmission(bool stop = true) { (void)stop; return _addr == 0x3B ? 0 : 2; }
  size_t requestFrom(int addr, int n, int stop = 1);
  size_t requestFrom(uint8_t addr, uint8_t n) { return requestFrom((int)addr, (int)n); }
  size_t write(uint8_t c) override { (void)c; return 1; }
  size_t write(const uint8_t *b, size_t n) override { (void)b; return n; }
  int available() override { return _rxn - _rxi; }
  int read() override { return _rxi < _rxn ? _rx[_rxi++] : -1; }
  int peek() override { return _rxi < _rxn ? _rx[_rxi] : -1; }
  void flush() override {}
  void setClock(uint32_t) {}
};
extern TwoWire Wire;
