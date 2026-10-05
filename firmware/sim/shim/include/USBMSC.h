#pragma once
#include <Arduino.h>
typedef int32_t (*msc_read_cb)(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize);
typedef int32_t (*msc_write_cb)(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize);
typedef bool (*msc_start_stop_cb)(uint8_t power_condition, bool start, bool load_eject);
// sim: the "Gotek" on the web page reads the presented disk through these callbacks.
class USBMSC {
public:
  USBMSC();
  bool begin(uint32_t block_count, uint16_t block_size);
  void end();
  void vendorID(const char *v);
  void productID(const char *p);
  void productRevision(const char *r);
  void onStartStop(msc_start_stop_cb cb);
  void onRead(msc_read_cb cb);
  void onWrite(msc_write_cb cb);
  void mediaPresent(bool media_present);
  void isWritable(bool w);
};
