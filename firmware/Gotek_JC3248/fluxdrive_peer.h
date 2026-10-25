// fluxdrive_peer.h - what the panel decides from a fleet peer's board.
//
// FluxDrive is an ESP32-S3 board that IS drive DF0 of an Amiga. It joins the fleet like a Webby dongle: same UDP
// beacons (51703), same TCP push (3333), same ESP-NOW pairing. It tells itself apart by "board":"fluxdrive" in its
// beacon and by board kind 4 in pad[2] of the pairing reply (Webby: 1 SuperMini, 2 Zero, 3 XIAO).
//
// What differs for the panel:
//  - it takes ONE image size, a DD ADF of 901,120 bytes, and refuses anything else before any change;
//  - while the Amiga's drive motor runs it neither accepts nor answers, so a push can sit in the TCP backlog for
//    the length of a game load, and so can the reply to SET_NAME;
//  - since v2 (writing) it takes the Amiga's writes and serves GET_SAVE as a Webby does. In home-Wi-Fi fleet mode the
//    panel fetches them over the LAN (its beacon says "dirty" and "load_id"), into the same Game.sav.<tag>.adf an ESP-NOW
//    pairing would use; which game file a save belongs to is kept in a record per FluxDrive (FdSaveRecord below).
//
// Pure functions of the board string, no Arduino types: tools/test_fluxdrive_peer.sh builds and runs them on the PC.
// Every other board gets exactly the answer the panel gave before this file existed.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define FD_KIND_FLUXDRIVE 4              // pad[2] of the pairing reply / board_id in /status
#define FD_DD_ADF_BYTES   901120u        // 80 cylinders x 2 heads x 11 sectors x 512 bytes
#define FD_BUSY_WAIT_MS   120000u        // longer than any game load the Amiga makes a push wait for
#define FD_STALE_MS       180000u        // silence a FluxDrive may keep and still be listed: more than the wait above

inline bool fdIsFluxDrive(const char* board) { return board != nullptr && strcmp(board, "fluxdrive") == 0; }

// nullptr = fine to send; otherwise the text the user gets, and nothing may be connected or sent.
inline const char* fdRefuseImage(const char* board, uint32_t bytes) {
  if (fdIsFluxDrive(board) && bytes != FD_DD_ADF_BYTES) return "FluxDrive takes DD ADFs only (901,120 bytes)";
  return nullptr;
}

// How long a push may make no progress (15 s for a dongle) before the panel gives up.
inline uint32_t fdStallLimitMs(const char* board) { return fdIsFluxDrive(board) ? FD_BUSY_WAIT_MS : 15000u; }

// How long to wait for a reply (the ack, the SET_NAME answer). normal_ms is what the panel waits for a dongle.
inline uint32_t fdReplyWaitMs(const char* board, uint32_t normal_ms) { return fdIsFluxDrive(board) ? FD_BUSY_WAIT_MS : normal_ms; }

// How long a peer may stay silent before the panel drops it from the fleet (40 s for a dongle, which beacons every 12 s).
// A FluxDrive sends no beacon while the Amiga's motor runs, so a game load must not make it vanish from the list.
inline uint32_t fdStaleMs(const char* board, uint32_t normal_ms) { return fdIsFluxDrive(board) ? FD_STALE_MS : normal_ms; }

// What a stalled push means for a FluxDrive: not a broken link, the Amiga is using the drive.
inline const char* fdBusyText() { return "FluxDrive busy - the Amiga is reading"; }

// A FluxDrive writes back since v2 (writing), as a dongle does: it has saves to pull. (It never answers 0x02, spec
// section 6, so the "unsaved saves" wording that goes with this never reaches a FluxDrive's user.)
inline bool fdHasSaves(const char* board) { (void)board; return true; }

// "dongle" in the words the panel says to the user; a FluxDrive is not one.
inline const char* fdDeviceWord(const char* board) { return fdIsFluxDrive(board) ? "FluxDrive" : "dongle"; }

// The board string a pairing reply's kind number stands for ("" = a Webby, or an old pairing that never kept it).
inline const char* fdBoardForKind(uint8_t kind) { return kind == FD_KIND_FLUXDRIVE ? "fluxdrive" : ""; }

// The mDNS name of a paired device is <prefix>-<mac bytes 4 and 5>.local: a Webby is gotekomega-, a FluxDrive fluxdrive-.
inline const char* fdHostPrefix(uint8_t kind) { return kind == FD_KIND_FLUXDRIVE ? "fluxdrive" : "gotekomega"; }

// ---- Fix round 1: what the person at the touchscreen sees while a FluxDrive is busy -------------------------------

#define FD_BUSY_MEMO_MS   10000u         // how long "it did not answer SET_NAME" is remembered for the push right behind it

// SET_NAME is the first thing sent for a disk. A FluxDrive that stayed silent for the WHOLE wait (not one whose
// connection closed early) is still busy, so the push behind it would only wait out the same game load again.
inline bool fdNameMeansBusy(const char* board, bool got_reply, bool wait_ran_out) {
  return fdIsFluxDrive(board) && !got_reply && wait_ran_out;
}

// One remembered address. note() when SET_NAME ran out; take() in the push to the same address says "busy" at once,
// once, and only while the note is fresh. clear() starts a new send. now is millis(); the wrap is handled.
struct FdBusyMemo {
  char ip[48];
  uint32_t at;
  bool set;
  FdBusyMemo() : at(0), set(false) { ip[0] = 0; }
  void clear() { set = false; ip[0] = 0; }
  void note(const char* addr, uint32_t now) {
    strncpy(ip, addr ? addr : "", sizeof(ip) - 1);
    ip[sizeof(ip) - 1] = 0;
    at = now;
    set = true;
  }
  bool take(const char* addr, uint32_t now, uint32_t ttl_ms) {
    if (!set) return false;
    if ((uint32_t)(now - at) > ttl_ms) { clear(); return false; }
    if (addr == nullptr || ip[0] == 0 || strncmp(ip, addr, sizeof(ip) - 1) != 0) return false;
    clear();
    return true;
  }
};

// Multi-select result line: "<name>: <text>" is centred without wrapping, so it clips at both ends when too wide.
// A FluxDrive's refusal is 44 characters; the name in front makes it about 60. For a FluxDrive the name is dropped
// when there is one target, or when the full line does not fit the screen (8 px margin each side).
// Every other board keeps "<name>: <text>" exactly as before.
inline bool fdKeepNamePrefix(const char* board, int targets, int full_text_px, int screen_px) {
  if (!fdIsFluxDrive(board)) return true;
  return targets > 1 && full_text_px <= screen_px - 8;
}

// What the fleet manager compares to decide whether to repaint: for a FluxDrive the Amiga's power, whether it holds a
// disk and the disk's name; for any other board nothing (its row repaints when its id list changes, as before).
// Writes "" or "|<A|a><L|l><disk>;" into out (cut to cap-1) and returns the length.
inline size_t fdRowSig(const char* board, bool amiga, bool loaded, const char* disk, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = 0;
  if (!fdIsFluxDrive(board)) return 0;
  const int n = snprintf(out, cap, "|%c%c%s;", amiga ? 'A' : 'a', loaded ? 'L' : 'l', disk ? disk : "");
  if (n < 0) { out[0] = 0; return 0; }
  return (size_t)n >= cap ? cap - 1 : (size_t)n;
}

// ---- Sync: the panel shows what the FluxDrive really holds (its beacon), and EJECT reaches it ----------------------

// The FluxDrive the main screen speaks for in fleet mode (NOW PLAYING, the loaded marker, EJECT): the peer the last
// disk of this boot went to, while it is a FluxDrive still heard; else the only FluxDrive heard; else none (-1).
// visible[i]: heard and shown; is_fd[i]: its beacon says "fluxdrive"; ids[i]: its beacon id; last_id: "" = no fling yet.
inline int fdPickTarget(int n, const bool* visible, const bool* is_fd, const char* const* ids, const char* last_id) {
  if (n <= 0 || visible == nullptr || is_fd == nullptr || ids == nullptr) return -1;
  if (last_id != nullptr && last_id[0])
    for (int i = 0; i < n; i++)
      if (visible[i] && is_fd[i] && ids[i] != nullptr && strcmp(ids[i], last_id) == 0) return i;
  int only = -1;
  for (int i = 0; i < n; i++) {
    if (!visible[i] || !is_fd[i]) continue;
    if (only >= 0) return -1;
    only = i;
  }
  return only;
}

inline bool fdNameBlank(char c) { return c == ' ' || c == 9 || c == 10 || c == 13; }   // space, tab, LF, CR
inline char fdNameLower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
// The part of a disk name that counts: no blanks around it, no ".adf" at its end (any case).
inline void fdNameCore(const char* s, const char** at, size_t* len) {
  *at = s ? s : ""; *len = strlen(*at);
  while (*len && fdNameBlank(**at)) { (*at)++; (*len)--; }
  while (*len && fdNameBlank((*at)[*len - 1])) (*len)--;
  if (*len >= 4 && (*at)[*len - 4] == '.' && fdNameLower((*at)[*len - 3]) == 'a' && fdNameLower((*at)[*len - 2]) == 'd' &&
      fdNameLower((*at)[*len - 1]) == 'f') *len -= 4;
  while (*len && fdNameBlank((*at)[*len - 1])) (*len)--;
}

// Two disk names are the same disk: blanks around them, case and one ".adf" at the end do not count. An empty name
// (a FluxDrive with no disk) is the same as nothing.
inline bool fdSameDiskName(const char* a, const char* b) {
  const char *pa, *pb; size_t na, nb;
  fdNameCore(a, &pa, &na); fdNameCore(b, &pb, &nb);
  if (na == 0 || na != nb) return false;
  for (size_t i = 0; i < na; i++) if (fdNameLower(pa[i]) != fdNameLower(pb[i])) return false;
  return true;
}

// A peer is still heard: its last beacon is less than stale_ms old (now and seen are millis(); the wrap is handled).
inline bool fdHeard(uint32_t now, uint32_t seen, uint32_t stale_ms) { return (uint32_t)(now - seen) < stale_ms; }

// What the screen marks as loaded (green marker, EJECT label), as the panel sees it at one moment:
//  target: fleet mode with a FluxDrive target (fdPickTarget); tloaded, tdisk: its beacon ("loaded", "disk");
//  staged: a disk is in this panel's RAM; sgame, sdisk: its game (-1 = none: a USR-DSK disk, the test kit) and disk;
//  sname: the name it was flung under.
struct FdUiView { bool target, tloaded; const char* tdisk; bool staged; int sgame, sdisk; const char* sname; };

// Is game gi (flung under gname, gdisks disks) shown as loaded, and which disk (*disk, -1 = not known)?
// No target: the game staged here, as before. A target: what it holds, by name, staged here or not. When it holds the
// disk staged here, only the staged game and disk are shown (not every game of that name); otherwise the staging is
// stale and gives way to the game of the name it reports. Every disk of a game is flung under the game's name, so the
// disk of a game not staged here is known only when the game has one disk. Names match whole (fdSameDiskName).
inline bool fdUiLoaded(const FdUiView& v, int gi, const char* gname, int gdisks, int* disk) {
  int none;
  if (disk == nullptr) disk = &none;
  *disk = -1;
  if (!v.target) {
    if (!v.staged || v.sgame != gi) return false;
    *disk = v.sdisk;
    return true;
  }
  if (!v.tloaded) return false;
  if (v.staged && fdSameDiskName(v.tdisk, v.sname)) {
    if (v.sgame != gi) return false;
    *disk = v.sdisk;
    return true;
  }
  if (!fdSameDiskName(v.tdisk, gname)) return false;
  *disk = gdisks <= 1 ? 0 : -1;
  return true;
}

// A screen rectangle: x, y its top-left pixel, w x h its size.
struct FdRect { int x, y, w, h; };

// A touch ends on NOW PLAYING: is it the EJECT of the FluxDrive target's disk? Only a tap (the finger did not move past
// the drag threshold: the bar sits right under the list, so a flick of its bottom row can start in it), inside the bar
// as drawn (now_on), with a target (fleet mode) that holds a disk.
inline bool fdBarEjects(bool now_on, int x, int y, FdRect bar, bool target, bool loaded, bool moved) {
  if (!now_on || !target || !loaded || moved) return false;
  return x >= bar.x && x < bar.x + bar.w && y >= bar.y && y < bar.y + bar.h;
}

// A command got no reply because the wait ran out (waited_ms of wait_ms), not because the connection closed early. A
// FluxDrive holds a command while the Amiga reads and runs it later: only this case may be called "busy".
inline bool fdReplyRanOut(bool got_reply, uint32_t waited_ms, uint32_t wait_ms) { return !got_reply && waited_ms >= wait_ms; }

// ---- Speed: the send buffer of a push to a FluxDrive ------------------------------------------------------------------

#define FD_SEND_SEGMENTS 16u             // FluxDrive's receive window: 16 segments (23,040 B)

// How large lwIP's send buffer (the tcp_pcb's snd_buf) should be for a push to this board, in bytes; 0 = leave lwIP's
// default alone. The default (TCP_SND_BUF, 5744 B = 4 segments) lets 4 segments be in flight, 5744 B per round trip:
// 58 kB/s on a mesh with ~99 ms RTT. A FluxDrive takes 16 segments. lwIP queues at most `queuelen` pbufs per connection
// (TCP_SND_QUEUELEN, one or more per segment), so the buffer stays one pbuf short of that queue, and within the 16 bits
// of snd_buf (no window scaling in this build). Every other board: 0.
inline uint32_t fdSendBufBytes(const char* board, uint32_t mss, uint32_t queuelen) {
  if (!fdIsFluxDrive(board) || mss == 0 || queuelen < 2) return 0;
  uint32_t segs = queuelen - 1;
  if (segs > FD_SEND_SEGMENTS) segs = FD_SEND_SEGMENTS;
  const uint32_t bytes = segs * mss;
  return bytes > 0xFFFFu ? 0xFFFFu : bytes;
}

// How much to add to a connection's lwIP send buffer (snd_buf) so it holds `want` bytes (fdSendBufBytes). 0 = leave it,
// and *why says why. Only a TCP socket (found) that is ESTABLISHED, goes to the port the push was meant for, and still
// sits exactly at lwIP's default (nothing queued on it yet); never lower, never past the 16 bits of snd_buf. The caller
// adds the difference, so whatever lwIP holds at that moment stays counted.
inline uint32_t fdSndBufDelta(uint32_t want, bool found, bool established, uint16_t remote_port, uint16_t port,
                              uint32_t snd_buf, uint32_t snd_buf_default, const char** why) {
  const char* w = "";
  uint32_t delta = 0;
  if (!found) w = "no TCP socket";
  else if (!established) w = "not established";
  else if (remote_port != port) w = "another port";
  else if (snd_buf != snd_buf_default) w = "not at lwIP's default";
  else if (want <= snd_buf) w = "not wanted";
  else if (want > 0xFFFFu) w = "too large";
  else delta = want - snd_buf;
  if (why) *why = w;
  return delta;
}

// ---- Wi-Fi: one gti.log line per join, not one per flap ---------------------------------------------------------------

#define FD_JOIN_LOG_MS 30000u            // the same access point is logged again only after this long

// Is a join to `bssid` worth a "[wifi] joined" line? The first of this boot, any change of access point, or the same
// one again after FD_JOIN_LOG_MS (a link that flaps does not fill the card). now / last_ms are millis(); wrap-safe.
inline bool fdJoinLogDue(bool logged_before, uint32_t now, uint32_t last_ms, const char* last_bssid, const char* bssid) {
  if (!logged_before) return true;
  if (strcmp(last_bssid ? last_bssid : "", bssid ? bssid : "") != 0) return true;
  return (uint32_t)(now - last_ms) >= FD_JOIN_LOG_MS;
}

// ---- CONFIG.TXT switches read at run time -------------------------------------------------------------------------------

// One CONFIG.TXT line against an on/off key, as loadConfig reads the file: KEY=VALUE, blanks around both, a line that
// starts with '#' is a comment. Returns -1 when the line is not this key; 0 for OFF, 0 or NO (any case); 1 otherwise
// (an empty or unknown value leaves the switch on).
inline int fdCfgSwitch(const char* line, const char* key) {
  if (line == nullptr || key == nullptr || key[0] == 0) return -1;
  while (fdNameBlank(*line)) line++;
  const size_t k = strlen(key);
  if (strncmp(line, key, k) != 0) return -1;
  const char* p = line + k;
  while (fdNameBlank(*p)) p++;
  if (*p != '=') return -1;
  p++;
  while (fdNameBlank(*p)) p++;
  size_t n = strlen(p);
  while (n && fdNameBlank(p[n - 1])) n--;
  const auto is = [&](const char* word) {
    const size_t m = strlen(word);
    if (m != n) return false;
    for (size_t i = 0; i < n; i++) if (fdNameLower(p[i]) != fdNameLower(word[i])) return false;
    return true;
  };
  return (is("off") || is("0") || is("no")) ? 0 : 1;
}


// ---- LAN saves (v2 writing): which game file a FluxDrive's save belongs to -----------------------------------------------

// What the panel keeps about the last disk a single-target fling put in a FluxDrive, one line per FluxDrive in
// /GTI/fdsaves.txt, so that after either side restarts it still knows where a fetched save belongs (spec section 6):
//  id    the FluxDrive's beacon id (its STA MAC, 12 upper-case hex digits);
//  uid   the disk's uid from the push ack (0x01 + LE32), which is the beacon's "load_id" while it holds that disk;
//  sent  the SD file whose bytes went out (its own save, the cable save or the image), the base the save is built on;
//  orig  the library image behind it, which names the save: Game.sav.<tag>.adf;
//  tag   the FluxDrive's save tag (its beacon "tag": softAP MAC bytes 4-5, upper-case hex).
// The line: id TAB uid TAB tag TAB sent TAB orig (uid in decimal). FAT names cannot hold a TAB or a control character.
struct FdSaveRecord { char id[13]; uint32_t uid; char sent[160]; char orig[160]; char tag[5]; };

inline bool fdRecUpperHex(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'); }
// A string held in a fixed array: it ends inside the array, and its length is in [lo, hi].
inline bool fdRecStrLen(const char* s, size_t cap, size_t lo, size_t hi, size_t* len) {
  const void* end = memchr(s, 0, cap);
  if (end == nullptr) return false;
  *len = (size_t)((const char*)end - s);
  return *len >= lo && *len <= hi;
}
inline bool fdRecHexField(const char* s, size_t cap, size_t lo, size_t hi) {
  size_t n;
  if (!fdRecStrLen(s, cap, lo, hi, &n)) return false;
  for (size_t i = 0; i < n; i++) if (!fdRecUpperHex(s[i])) return false;
  return true;
}
// An SD path: from the card's root, no control characters (a TAB or a newline would break the line).
inline bool fdRecPathField(const char* s, size_t cap) {
  size_t n;
  if (!fdRecStrLen(s, cap, 1, cap - 1, &n) || s[0] != '/') return false;
  for (size_t i = 0; i < n; i++) if ((unsigned char)s[i] < 0x20 || s[i] == 0x7F) return false;
  return true;
}
// A record that can be written and matched: an id, a uid (0 = none: matches nothing), a 4-digit tag, two SD paths.
inline bool fdRecordValid(const FdSaveRecord& r) {
  return fdRecHexField(r.id, sizeof r.id, 1, sizeof r.id - 1) && r.uid != 0 && fdRecHexField(r.tag, sizeof r.tag, 4, 4) &&
         fdRecPathField(r.sent, sizeof r.sent) && fdRecPathField(r.orig, sizeof r.orig);
}

// The record as one line (no newline) into out. Returns its length, or -1 when the record is not valid or does not fit
// whole (out is then "" when it has room for that).
inline int fdRecordFormat(const FdSaveRecord& r, char* out, size_t n) {
  if (out == nullptr || n == 0) return -1;
  out[0] = 0;
  if (!fdRecordValid(r)) return -1;
  const int w = snprintf(out, n, "%s\t%lu\t%s\t%s\t%s", r.id, (unsigned long)r.uid, r.tag, r.sent, r.orig);
  if (w < 0 || (size_t)w >= n) { out[0] = 0; return -1; }
  return w;
}

// One line of the file into a record. A newline (and CRs) at its end do not count. Anything that is not exactly the
// five fields fdRecordFormat writes is refused, and r is then left as it was.
inline bool fdRecordParse(const char* line, FdSaveRecord& r) {
  if (line == nullptr) return false;
  size_t len = strlen(line);
  while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
  const char* f[5]; size_t fn[5]; int k = 0; size_t start = 0;
  for (size_t i = 0; i <= len; i++) {
    if (i < len && line[i] != '\t') continue;
    if (k == 5) return false;                                     // a sixth field
    f[k] = line + start; fn[k] = i - start; k++; start = i + 1;
  }
  if (k != 5) return false;
  FdSaveRecord t;
  memset(&t, 0, sizeof t);
  if (fn[1] == 0 || fn[1] > 10) return false;
  uint64_t uid = 0;
  for (size_t i = 0; i < fn[1]; i++) {
    if (f[1][i] < '0' || f[1][i] > '9') return false;
    uid = uid * 10 + (uint64_t)(f[1][i] - '0');
  }
  if (uid > 0xFFFFFFFFull) return false;
  t.uid = (uint32_t)uid;
  if (fn[0] >= sizeof t.id || fn[2] >= sizeof t.tag || fn[3] >= sizeof t.sent || fn[4] >= sizeof t.orig) return false;
  memcpy(t.id, f[0], fn[0]); memcpy(t.tag, f[2], fn[2]); memcpy(t.sent, f[3], fn[3]); memcpy(t.orig, f[4], fn[4]);
  if (!fdRecordValid(t)) return false;
  r = t;
  return true;
}

// A record from what a fling knew, whole or not at all (a cut path would name another file): false, and r left as it
// was, when a field does not fit or the result is not a valid record (no uid from the ack, no tag, no SD file).
inline bool fdRecordSet(FdSaveRecord& r, const char* id, uint32_t uid, const char* sent, const char* orig, const char* tag) {
  if (id == nullptr || sent == nullptr || orig == nullptr || tag == nullptr) return false;
  FdSaveRecord t;
  memset(&t, 0, sizeof t);
  if (strlen(id) >= sizeof t.id || strlen(sent) >= sizeof t.sent || strlen(orig) >= sizeof t.orig || strlen(tag) >= sizeof t.tag)
    return false;
  strcpy(t.id, id); strcpy(t.sent, sent); strcpy(t.orig, orig); strcpy(t.tag, tag);
  t.uid = uid;
  if (!fdRecordValid(t)) return false;
  r = t;
  return true;
}

// Fetch this FluxDrive's unsaved sectors now? Only when there are some (its beacon's "dirty") and they belong to the disk
// this panel sent it: a record (rec) whose uid is the beacon's "load_id". Another disk (a PC push, its web page) or none
// in the drive: no.
inline bool fdShouldFetch(uint16_t dirty, uint32_t beacon_uid, const FdSaveRecord* rec) {
  return dirty > 0 && rec != nullptr && beacon_uid != 0 && rec->uid == beacon_uid;
}
