#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

static portMUX_TYPE gotekDiskMutex = portMUX_INITIALIZER_UNLOCKED;
class GotekDiskGuard {
public:
  GotekDiskGuard() { portENTER_CRITICAL(&gotekDiskMutex); }
  ~GotekDiskGuard() { portEXIT_CRITICAL(&gotekDiskMutex); }
  GotekDiskGuard(const GotekDiskGuard&) = delete;
  GotekDiskGuard& operator=(const GotekDiskGuard&) = delete;
};
