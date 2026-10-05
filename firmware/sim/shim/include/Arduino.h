// GTi browser simulator - Arduino core shim (wasm32). Only what the GTi firmware uses.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include "sim_host.h"
#include "pgmspace.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp32-hal.h"
typedef bool boolean;
typedef uint8_t byte;
typedef unsigned int word;

#ifdef __cplusplus
#include <algorithm>
#include <cmath>
#include "WCharacter.h"
#include "WString.h"
#include "Stream.h"
#include "Printable.h"
#include "Print.h"
using std::min; using std::max; using std::isinf; using std::isnan;
using std::abs;
#define _min(a,b) ((a)<(b)?(a):(b))
#define _max(a,b) ((a)>(b)?(a):(b))
#endif

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define PI 3.1415926535897932384626433832795
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
#define radians(deg) ((deg)*DEG_TO_RAD)
#define degrees(rad) ((rad)*RAD_TO_DEG)
#define sq(x) ((x)*(x))
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define lowByte(w) ((uint8_t) ((w) & 0xff))
#define highByte(w) ((uint8_t) ((w) >> 8))
#define IRAM_ATTR
#define DRAM_ATTR
#define EXT_RAM_BSS_ATTR
#define RTC_DATA_ATTR
#define RTC_NOINIT_ATTR __attribute__((used))
#define ARDUINO_ISR_ATTR
#define digitalPinToInterrupt(p) (p)

#ifdef __cplusplus
extern "C" {
#endif
unsigned long millis(void);
unsigned long micros(void);
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
void yield(void);
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);
int analogRead(uint8_t pin);
float temperatureRead(void);
const char *pathToFileName(const char *path);
void *ps_malloc(size_t size);
void *ps_calloc(size_t n, size_t size);
void *ps_realloc(void *ptr, size_t size);
bool psramFound(void);
bool ledcAttach(uint8_t pin, uint32_t freq, uint8_t resolution);
bool ledcWrite(uint8_t pin, uint32_t duty);
#ifdef __cplusplus
}
long random(long);
long random(long, long);
void randomSeed(unsigned long);
long map(long, long, long, long, long);

class SimSerial : public Stream {
public:
  void begin(unsigned long, int = 0, int = -1, int = -1) {}
  void end() {}
  void setTxTimeoutMs(uint32_t) {}
  void setDebugOutput(bool) {}
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *buf, size_t n) override;
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  operator bool() const { return true; }
};
extern SimSerial Serial;

class EspClass {
public:
  uint32_t getFreeHeap();
  uint32_t getHeapSize() { return 320 * 1024; }
  uint32_t getMinFreeHeap() { return getFreeHeap(); }
  uint32_t getMaxAllocHeap() { return 120 * 1024; }
  uint32_t getFreePsram();
  uint32_t getPsramSize() { return 8 * 1024 * 1024; }
  uint32_t getMaxAllocPsram() { return getFreePsram(); }
  uint32_t getMinFreePsram() { return getFreePsram(); }
  void restart();
  const char *getChipModel() { return "ESP32-S3 (simulated)"; }
  uint8_t getChipRevision() { return 2; }
  uint8_t getChipCores() { return 2; }
  uint32_t getCpuFreqMHz() { return 240; }
  uint32_t getFlashChipSize() { return 16 * 1024 * 1024; }
  uint32_t getSketchSize() { return 1931674; }
  uint32_t getFreeSketchSpace() { return 0x6E0000 - 1931674; }
  uint64_t getEfuseMac();
  const char *getSdkVersion() { return "sim"; }
};
extern EspClass ESP;
#endif
