#pragma once
//
// Streaming multipart/form-data body reader.
//
// Split out of webserver.h so it can be exercised on the host: it is the path a
// disk image travels on its way into the device, and it was wrong in three ways
// that only show up with binary payloads. See tools/test/test_multipart.cpp.
//
// The rules it has to obey:
//
//  * Binary-safe. The payload is a disk image full of NUL bytes. Anything built
//    on strstr — which is what Arduino's String::indexOf uses — stops at the
//    first zero and never sees the delimiter that follows.
//  * Correct across reads. A delimiter can land with half its bytes in one TCP
//    read and half in the next, so the tail of each read has to be held back
//    until enough has arrived to rule it out.
//  * Never emit delimiter bytes. A missed delimiter puts it, and the multipart
//    epilogue after it, inside the file — and the transfer then only ends on a
//    stall timeout.

#include <stdint.h>
#include <string.h>

// Binary-safe substring search. Returns the offset of `needle` in `hay`, or -1.
inline int mpFindBytes(const uint8_t *hay, int hayLen,
                       const char *needle, int needleLen) {
  if (!hay || !needle || needleLen <= 0 || hayLen < needleLen) return -1;
  const uint8_t first = (uint8_t)needle[0];
  for (int i = 0; i <= hayLen - needleLen; i++) {
    if (hay[i] != first) continue;
    if (memcmp(hay + i, needle, (size_t)needleLen) == 0) return i;
  }
  return -1;
}

// Longest delimiter we will scan for. RFC 2046 caps a boundary at 70
// characters; the delimiter is "--" plus that.
#define MP_MAX_DELIM 80

class MultipartBody {
 public:
  // `delim` is the delimiter WITHOUT the trailing CRLF, i.e. "--" + boundary.
  bool begin(const char *delim, int delimLen) {
    if (delimLen <= 0 || delimLen > MP_MAX_DELIM) return false;
    memcpy(_delim, delim, (size_t)delimLen);
    _dl    = delimLen;
    _held  = 0;
    _done  = false;
    _final = false;
    return true;
  }

  // Feed one read. Payload is handed to sink(const uint8_t*, int) in pieces —
  // possibly zero times, possibly several. Returns true once the delimiter has
  // been reached, after which further feeds are ignored.
  template <class Sink>
  bool feed(const uint8_t *data, int len, Sink sink) {
    while (len > 0 && !_done) {
      const int room = CAP - _held;
      const int take = (len < room) ? len : room;
      memcpy(_win + _held, data, (size_t)take);
      data += take;
      len  -= take;
      const int have = _held + take;

      const int idx = mpFindBytes(_win, have, _delim, _dl);
      if (idx >= 0) {
        // The CRLF immediately before the delimiter belongs to the framing,
        // not to the file.
        const int emit = (idx >= 2) ? idx - 2 : 0;
        if (emit > 0) sink(_win, emit);
        _done = true;
        // "--" straight after the delimiter marks the last part. It may not
        // have arrived yet, in which case the caller's header loop sees it.
        if (have >= idx + _dl + 2 &&
            _win[idx + _dl] == '-' && _win[idx + _dl + 1] == '-') _final = true;
        return true;
      }

      // Nothing found: emit everything except the tail that could still be the
      // opening bytes of a delimiter split across this read and the next.
      const int carry = _dl + 2;
      const int flush = (have > carry) ? have - carry : 0;
      if (flush > 0) {
        sink(_win, flush);
        _held = have - flush;
        if (_held > 0) memmove(_win, _win + flush, (size_t)_held);
      } else {
        _held = have;
      }
    }
    return _done;
  }

  bool done() const { return _done; }
  bool sawFinalDelimiter() const { return _final; }

 private:
  // One TCP read plus room for the held-back tail.
  static const int CAP = 1024 + MP_MAX_DELIM + 2;
  uint8_t _win[CAP];
  char    _delim[MP_MAX_DELIM];
  int     _dl    = 0;
  int     _held  = 0;
  bool    _done  = false;
  bool    _final = false;
};
