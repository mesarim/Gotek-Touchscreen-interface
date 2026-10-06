// sim stub of shared/webdav_client.h: no network in the browser simulator, every remote fetch says so.
#pragma once
#include <Arduino.h>
#include <WiFi.h>
struct DavConfig {
  String host; uint16_t port = 443; bool https = true; String user; String pass; String basePath = "/"; bool enabled = false;
};
using DavLogFn = void (*)(const String &msg);
class GotekDAV {
  DavConfig _c; String _err = "no network in the simulator";
public:
  void configure(const DavConfig &c, DavLogFn fn) { _c = c; (void)fn; }
  bool connect() { return false; }
  long streamToBuffer(const String &, uint8_t *, size_t, bool) { delay(400); return -1; }
  void closeIdle() {}
  bool lastTruncated() { return false; }
  String lastError() { return _err; }
};
static GotekDAV davClient;
