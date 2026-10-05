#pragma once
#include <Arduino.h>
#include "esp_event.h"
typedef enum { ARDUINO_USB_ANY_EVENT = -1, ARDUINO_USB_STARTED_EVENT = 0, ARDUINO_USB_STOPPED_EVENT, ARDUINO_USB_SUSPEND_EVENT, ARDUINO_USB_RESUME_EVENT, ARDUINO_USB_MAX_EVENT } arduino_usb_event_t;
class ESPUSB {
public:
  bool begin();
  void onEvent(esp_event_handler_t cb) { (void)cb; }
  void onEvent(arduino_usb_event_t e, esp_event_handler_t cb) { (void)e; (void)cb; }
  bool VID(uint16_t) { return true; } bool PID(uint16_t) { return true; }
  bool productName(const char *) { return true; } bool manufacturerName(const char *) { return true; }
  operator bool() const { return true; }
};
extern ESPUSB USB;

