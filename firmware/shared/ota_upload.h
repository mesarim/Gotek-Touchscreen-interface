#pragma once
#include <Update.h>

// One application image per HTTP request. Activate it only after the entire
// multipart request completes, and release Update's buffers on every failure.
class GotekOtaUpload {
  enum State { Idle, Receiving, Complete, Failed } state = Idle;
  size_t received = 0;
  int errorCode = 400;
  const char* errorText = "No complete firmware image was uploaded";

  void updateFailed() {
    uint8_t error = Update.getError();
    if (error == UPDATE_ERROR_OK)
      reject(503, "Not enough memory to update firmware; please retry");
    else if (error == UPDATE_ERROR_NO_PARTITION)
      reject(409, "No spare firmware slot; install the full image over USB");
    else
      reject(500, Update.errorString());
  }
public:
  bool start() {
    if (state != Idle) {
      reject(400, "Upload exactly one firmware image per request");
      return false;
    }
    received = 0;
    errorCode = 400;
    errorText = "No complete firmware image was uploaded";
    state = Receiving;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      updateFailed();
      return false;
    }
    return true;
  }
  void reject(int code, const char* message) {
    Update.abort();
    state = Failed;
    errorCode = code;
    errorText = message;
  }
  void write(uint8_t* data, size_t bytes) {
    if (state != Receiving || !bytes) return;
    if (!received && data[0] != 0xE9) {
      reject(400, "Not an ESP32 application image; select the .ino.bin file");
      return;
    }
    if (bytes > Update.remaining()) {
      reject(413, "Image exceeds the firmware slot; use .ino.bin, not .merged.bin");
      return;
    }
    if (Update.write(data, bytes) != bytes) {
      updateFailed();
      return;
    }
    received += bytes;
  }
  void end(size_t total) {
    if (state != Receiving) return;
    if (!received || received != total) {
      reject(400, "Firmware upload is empty or incomplete; please retry");
      return;
    }
    state = Complete;
  }
  void abort() {
    Update.abort();
    state = Idle;
    received = 0;
    errorCode = 400;
    errorText = "Firmware upload was interrupted; please retry";
  }
  int consume() {
    int code = errorCode;
    if (state == Complete) {
      if (Update.end(true)) code = 200;
      else { updateFailed(); code = errorCode; }
    } else {
      Update.abort();
    }
    state = Idle;
    received = 0;
    return code;
  }
  const char* error() const { return errorText; }
};
