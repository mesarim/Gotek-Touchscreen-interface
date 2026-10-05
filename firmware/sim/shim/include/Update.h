#pragma once
#include <Arduino.h>
#define U_FLASH 0
#define U_SPIFFS 100
#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF
#define UPDATE_ERROR_OK 0
class UpdateClass {
  size_t _size = 0, _done = 0; uint8_t _err = 0;
public:
  bool begin(size_t size = UPDATE_SIZE_UNKNOWN, int cmd = U_FLASH, int ledPin = -1, uint8_t ledOn = 0, const char *label = NULL);
  size_t write(uint8_t *data, size_t len);
  bool end(bool evenIfRemaining = false);
  void abort() { _err = 8; }
  uint8_t getError() { return _err; }
  const char *errorString();
  bool hasError() { return _err != 0; }
  bool isFinished() { return _done == _size; }
  size_t progress() { return _done; }
  size_t size() { return _size; }
  size_t remaining() { return _size - _done; }
  void printError(Print &out) { out.println(errorString()); }
};
extern UpdateClass Update;
