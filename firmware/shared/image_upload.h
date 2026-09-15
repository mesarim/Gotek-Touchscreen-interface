#pragma once
#include <stddef.h>

// One multipart file per request. A responder must consume a matching END;
// bytes left over from an earlier request never authorize a new mount.
class GotekImageUpload {
  enum State { Idle, Receiving, Complete, Failed } state = Idle;
  size_t received = 0;
  int error = 400;
public:
  bool start() {
    if (state != Idle) { reject(400); return false; }
    state = Receiving; received = 0; error = 400; return true;
  }
  void reject(int code) { state = Failed; error = code; }
  bool write(size_t bytes, size_t limit) {
    if (state != Receiving) return false;
    if (received > limit || bytes > limit - received) { reject(413); return false; }
    received += bytes; return true;
  }
  void end(size_t total) {
    if (state == Receiving && total == received && total) state = Complete;
    else if (state != Failed) reject(400);
  }
  void abort() { state = Idle; received = 0; }
  int consume() {
    int code = state == Complete ? 200 : state == Failed ? error : 400;
    abort(); return code;
  }
};
