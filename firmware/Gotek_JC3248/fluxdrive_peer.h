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
// disk, the disk's name, its write protect (the row's WP key, v2 writing) and whether the row says STORING... (fd3, the
// commit window: fdShowStoring); for any other board nothing (its row repaints when its id list changes, as before).
// Writes "" or "|<A|a><L|l><W|w><S|s><disk>;" into out (cut to cap-1) and returns the length.
inline size_t fdRowSig(const char* board, bool amiga, bool loaded, const char* disk, char* out, size_t cap, bool wp = false,
                       bool storing = false) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = 0;
  if (!fdIsFluxDrive(board)) return 0;
  const int n = snprintf(out, cap, "|%c%c%c%c%s;", amiga ? 'A' : 'a', loaded ? 'L' : 'l', wp ? 'W' : 'w', storing ? 'S' : 's',
                         disk ? disk : "");
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
// Before a swap (another disk flung to it, or an eject), fetch? Whenever the disk in it is the one this panel sent it: it
// holds one (the beacon's "loaded"; an ejected disk's sectors cannot be fetched, though FluxDrive keeps its uid) and its
// uid is the record's. No dirty count: that comes only from a beacon, FluxDrive sends none while the Amiga's motor runs,
// and the panel zeroes it after its own fling, so writes made since would be missed. An empty fetch is one round trip.
inline bool fdFetchBeforeSwap(bool loaded, uint32_t beacon_uid, const FdSaveRecord* rec) {
  return loaded && rec != nullptr && beacon_uid != 0 && rec->uid == beacon_uid;
}

// A FluxDrive save tag (its beacon's "tag": the softAP MAC's last two bytes) names Game.sav.<tag>.adf: exactly 4 upper-case
// hex digits. Anything else (an old FluxDrive sends none) names no save.
inline bool fdSaveTagOk(const char* tag) {
  return tag != nullptr && strlen(tag) == 4 && fdRecUpperHex(tag[0]) && fdRecUpperHex(tag[1]) && fdRecUpperHex(tag[2]) &&
         fdRecUpperHex(tag[3]);
}
// Which file a fling sends to a target: `own` (its own save, Game.sav.<tag>.adf) when the target is a FluxDrive and the
// only one, saves are on, it has a save tag (fdSaveTagOk) and that file exists; else `staged`, the file staged here (the
// cable save or the image). Several targets get the staged file, as a Hivemind fan-out does.
// reset (fd2 fix round 1): that FluxDrive was STARTED FRESH on this game and has no own save since (its mark, FdFreshSet):
// the one target gets `image`, the original, instead of the staged cable save. An own save always wins.
inline const char* fdFileToSend(int targets, bool is_fd, bool saves_on, const char* tag, bool own_exists, const char* own,
                                const char* staged, bool reset = false, const char* image = nullptr) {
  const bool one = targets == 1 && is_fd && saves_on && fdSaveTagOk(tag);
  if (one && own_exists && own != nullptr && own[0]) return own;
  if (one && reset && image != nullptr && image[0]) return image;
  return staged;
}

// ---- Panel controls (v2 writing): start fresh, write protect -------------------------------------------------------------

// START FRESH throws away the save of a game's disk (spec section 6). Does the FluxDrive target get the original at once
// (the Amiga sees a swap)? Only when the disk in it now is the one this panel's record says it sent: it holds one (the
// beacon's "loaded"; an ejected disk keeps its uid) and its uid (the beacon's "load_id") is the record's. The caller passes
// the record only when it names this disk's file. Another disk, an empty drive, no record: no swap, the save just goes.
inline bool fdStartFreshResends(const FdSaveRecord* rec, uint32_t beacon_uid, bool loaded) {
  return rec != nullptr && loaded && beacon_uid != 0 && rec->uid == beacon_uid;
}
// Before a swap (a fling to a FluxDrive, an eject), fetch its unsaved writes first? Every swap as fdFetchBeforeSwap says,
// except START FRESH's fling of the original: those writes belong to the save it throws away (fetched, they would start a
// new save of the game), and the original replaces them (FluxDrive counts them as discarded).
inline bool fdFetchFirstNeeded(bool start_fresh, bool loaded, uint32_t beacon_uid, const FdSaveRecord* rec) {
  return !start_fresh && fdFetchBeforeSwap(loaded, beacon_uid, rec);
}
// Two SD paths name the same file: whole, case does not count (FAT). Empty or missing names nothing.
inline bool fdPathSame(const char* a, const char* b) {
  if (a == nullptr || b == nullptr || a[0] == 0) return false;
  for (; *a && *b; a++, b++) if (fdNameLower(*a) != fdNameLower(*b)) return false;
  return *a == 0 && *b == 0;
}
// What a record's FluxDrive is doing (fd2 fix round 1), from its beacon: not heard - or STANDALONE, which keeps no roster -
// is unknown; heard with a disk in it whose uid ("load_id") is the record's: it holds that disk; heard otherwise (another
// disk, ejected - FluxDrive keeps the uid -, no uid): other.
enum { FD_HOLDS_UNKNOWN = 0, FD_HOLDS_IT = 1, FD_HOLDS_OTHER = 2 };
inline int fdPeerHolds(bool heard, bool loaded, uint32_t beacon_uid, uint32_t rec_uid) {
  if (!heard) return FD_HOLDS_UNKNOWN;
  return (loaded && beacon_uid != 0 && beacon_uid == rec_uid) ? FD_HOLDS_IT : FD_HOLDS_OTHER;
}
// START FRESH deletes the save file `gone`. What becomes of a FluxDrive record whose base (`sent`, the file its Amiga was
// given; its next fetch rebuilds from it) is that file? 0: nothing (another base). 1: the record goes. 2: the base becomes
// its own save first (what its next fetch would have rebuilt from it anyway), so it is not left pointing at nothing.
//  - Its base is its own save (`own`, Game.sav.<tag>.adf of its game), the one that goes: 1 - its disk's writes belong to it.
//  - Its own save is another file: 2 only when that FluxDrive is heard holding that very disk (holds, fdPeerHolds), or its
//    state is unknown and it has no own save yet (own_exists false); otherwise 1, and its own save stays as it is: the
//    rebase would move it to .bak (dropping the older .bak) for a record that is dead or may be.
inline int fdFreshRecordFate(const char* sent, const char* own, const char* gone, int holds, bool own_exists) {
  if (!fdPathSame(sent, gone)) return 0;
  if (fdPathSame(own, gone)) return 1;
  if (holds == FD_HOLDS_IT) return 2;
  if (holds == FD_HOLDS_UNKNOWN && !own_exists) return 2;
  return 1;
}

// START FRESH is offered (fd2 fix round 1) in STANDALONE (wireless off) and in fleet mode (wireless over home Wi-Fi), never
// in ESP-NOW mode (wireless without home Wi-Fi): there it would eject the paired dongle's disk and fetch its writes into a
// save built from the one thrown away, while nothing is sent again. ESP-NOW behaves as before Task 8.
inline bool fdFreshOffered(bool wireless, bool link_home) { return !wireless || link_home; }

// After START FRESH on a FluxDrive (fd2 fix round 1): a mark per FluxDrive tag and image says "send it the original" -
// fdFileToSend's `reset` - until that FluxDrive has an own save of the game again (the first fetch that writes it, or a
// fling that sends one). Kept on the card, one line per mark: "fresh TAB tag TAB image". A full table forgets the oldest.
#define FD_FRESH_MAX 16
struct FdFreshMark { char tag[5]; char image[160]; };
struct FdFreshSet { FdFreshMark m[FD_FRESH_MAX]; int n; };
inline void fdFreshClearAll(FdFreshSet& s) { s.n = 0; }
// A mark that can be kept: a save tag (fdSaveTagOk's form) and an SD path from the root without control characters.
inline bool fdFreshMarkValid(const FdFreshMark& m) {
  return fdRecHexField(m.tag, sizeof m.tag, 4, 4) && fdRecPathField(m.image, sizeof m.image);
}
// Its index, or -1. The tag exactly, the image as a whole path (case does not count: FAT).
inline int fdFreshFind(const FdFreshSet& s, const char* tag, const char* image) {
  if (tag == nullptr || image == nullptr) return -1;
  for (int i = 0; i < s.n && i < FD_FRESH_MAX; i++)
    if (strcmp(s.m[i].tag, tag) == 0 && fdPathSame(s.m[i].image, image)) return i;
  return -1;
}
// true = it was there and is gone.
inline bool fdFreshDrop(FdFreshSet& s, const char* tag, const char* image) {
  const int k = fdFreshFind(s, tag, image);
  if (k < 0) return false;
  for (int i = k + 1; i < s.n; i++) s.m[i - 1] = s.m[i];
  s.n--;
  return true;
}
// true = it is in the table (it was, or is now). Not a valid mark, or a path that does not fit whole (a cut path would name
// another file): false, nothing changes.
inline bool fdFreshAdd(FdFreshSet& s, const char* tag, const char* image) {
  if (tag == nullptr || image == nullptr || strlen(tag) >= sizeof s.m[0].tag || strlen(image) >= sizeof s.m[0].image) return false;
  FdFreshMark t;
  memset(&t, 0, sizeof t);
  strcpy(t.tag, tag); strcpy(t.image, image);
  if (!fdFreshMarkValid(t)) return false;
  if (fdFreshFind(s, tag, image) >= 0) return true;
  if (s.n >= FD_FRESH_MAX) { for (int i = 1; i < FD_FRESH_MAX; i++) s.m[i - 1] = s.m[i]; s.n = FD_FRESH_MAX - 1; }
  s.m[s.n++] = t;
  return true;
}
// The mark as one line (no newline) into out: its length, or -1 (out "" when it has room) for an invalid mark or one that
// does not fit whole.
inline int fdFreshFormat(const FdFreshMark& m, char* out, size_t n) {
  if (out == nullptr || n == 0) return -1;
  out[0] = 0;
  if (!fdFreshMarkValid(m)) return -1;
  const int w = snprintf(out, n, "fresh\t%s\t%s", m.tag, m.image);
  if (w < 0 || (size_t)w >= n) { out[0] = 0; return -1; }
  return w;
}
// One line into a mark: exactly "fresh TAB tag TAB image", a newline (and CRs) at its end not counted. Anything else -
// a save record line among them - is refused and m left as it was.
inline bool fdFreshParse(const char* line, FdFreshMark& m) {
  if (line == nullptr) return false;
  size_t len = strlen(line);
  while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
  if (len < 6 || strncmp(line, "fresh\t", 6) != 0) return false;
  const char* tag = line + 6;
  const char* tab = (const char*)memchr(tag, '\t', len - 6);
  if (tab == nullptr) return false;
  const size_t tn = (size_t)(tab - tag), in = len - 6 - tn - 1;
  FdFreshMark t;
  memset(&t, 0, sizeof t);
  if (tn >= sizeof t.tag || in >= sizeof t.image) return false;
  memcpy(t.tag, tag, tn); memcpy(t.image, tab + 1, in);
  if (!fdFreshMarkValid(t)) return false;   // a further TAB in the image is a control character: refused here
  m = t;
  return true;
}

// SET_WP (0x10 + 1 byte: 1 on, 0 off) makes the change a re-insert on FluxDrive (the drive empty for 1 s, then the disk back
// with the change latch set: AmigaDOS reads the tab when a disk comes in) and answers after it. What the answer means:
// nullptr = done (0x01); otherwise why not. 0x00: no disk in it, nothing changed. No answer for the whole wait (ran_out):
// busy, FD_WP_BUSY - FluxDrive holds commands while the Amiga reads and makes the change after the load; its beacon's "wp"
// shows it then, and that is what the screen goes by. The connection closed early without one: "no reply". Another byte:
// "refused".
#define FD_WP_BUSY "busy - it changes when the Amiga stops"
inline const char* fdWpFailText(int reply, bool ran_out) {
  if (reply == 0x01) return nullptr;
  if (reply == 0x00) return "no disk in it";
  if (reply < 0) return ran_out ? FD_WP_BUSY : "no reply";
  return "refused";
}

// ---- The commit window (fd3, spec section 6) --------------------------------------------------------------------------
// FluxDrive writes a new disk to flash in steps, only while the Amiga's motor is off: about 8 s idle, up to 82 s while a game
// loads. A power cut before the last step brings the PREVIOUS disk back, with its uid and unsaved map. Its beacon's "stored"
// says when that window is closed: true once the disk in it is the one a power cut would bring back (loaded from flash at its
// boot, or its commit finished); false while a new disk's commit runs, after a commit gave up, and with no disk in it. It
// stays true during a journal compaction, and FluxDrive beacons at once when it turns true.

// The beacon's "stored" value as pfJf reads it: false only for FluxDrive's own "false". Missing ("") counts as stored: a
// Webby and a FluxDrive older than writing 10 never say it, and the panel knows of no commit window there.
inline bool fdStoredField(const char* v) { return !(v != nullptr && strcmp(v, "false") == 0); }

// "STORING..." (NOW PLAYING, the fleet row) and "do not switch off yet" (the status bar): a FluxDrive holding a disk that is
// not in flash yet. An empty FluxDrive also says stored false, and never shows it.
inline bool fdShowStoring(bool is_fd, bool loaded, bool stored) { return is_fd && loaded && !stored; }

// Until the disk a fling sent is stored, the panel keeps in RAM the record that fling replaced (prev; panel_fleet.h). What a
// beacon of that FluxDrive (its load_id, loaded, stored) means for it, with cur its record now:
//  Restore  - a disk in it whose uid is prev's: it was switched off before the new disk was stored and went back to prev.
//             prev is the record again (its writes are fetched as before), and the panel says so.
//  DropPrev - a disk in it whose uid is cur's, and stored: prev can never come back; forget it.
//  Keep     - anything else: the new disk's commit still runs or gave up, no disk in it (an eject inside the window says
//             stored false and keeps the ejected uid: it neither drops prev nor brings it back), another disk, no uid.
// Only "loaded && stored && uid == cur's" says the new disk is safe. cur and prev belong to one FluxDrive (the same id).
enum class FdRevert : uint8_t { Keep, DropPrev, Restore };
// The same on bare uids (0 = none): fdRevertCheck's records, and a pending START FRESH's fresh and prior disk.
inline FdRevert fdRevertUids(uint32_t cur_uid, uint32_t prev_uid, uint32_t beacon_uid, bool loaded, bool stored) {
  if (prev_uid == 0 || !loaded || beacon_uid == 0) return FdRevert::Keep;
  if (beacon_uid == cur_uid) return stored ? FdRevert::DropPrev : FdRevert::Keep;   // the new disk (never a revert)
  return beacon_uid == prev_uid ? FdRevert::Restore : FdRevert::Keep;
}
inline FdRevert fdRevertCheck(const FdSaveRecord* cur, const FdSaveRecord* prev, uint32_t beacon_uid, bool loaded, bool stored) {
  if (prev == nullptr) return FdRevert::Keep;
  if (cur != nullptr && strncmp(cur->id, prev->id, sizeof cur->id) != 0) return FdRevert::Keep;   // two FluxDrives: no pair
  return fdRevertUids(cur ? cur->uid : 0, prev->uid, beacon_uid, loaded, stored);
}

// Fix round 1: per FluxDrive a short list of the records flings replaced since its disk was last seen stored, oldest first.
// One is not enough: a disk B can be stored without the panel seeing it (ejected while its commit finished - "stored" needs
// a disk in the drive -, a beacon dropped after a fetch, fling or eject, UDP loss), so a power cut in the next fling's window
// can bring back B or the disk before it. A full list forgets the oldest.
#define FD_PREV_MAX 3
struct FdPrevList { FdSaveRecord r[FD_PREV_MAX]; int n; };
inline void fdPrevClear(FdPrevList& l) { l.n = 0; }
// Two records of one game on one FluxDrive: the same image (whole path, case does not count) and the same save tag, so the
// same own save Game.sav.<tag>.adf.
inline bool fdSameGame(const FdSaveRecord& a, const FdSaveRecord& b) {
  return fdPathSame(a.orig, b.orig) && strcmp(a.tag, b.tag) == 0;
}
enum { FD_PREV_NONE = 0, FD_PREV_KEPT = 1, FD_PREV_SAME_GAME = 2 };
// What fdPrevAtFling also did: kept records of the new disk's game dropped; the oldest dropped for room (ev).
struct FdPrevOut { int same_dropped; bool evicted; FdSaveRecord ev; };
// A fling of `now` (its new record) replaces `old` (the record it had, or nullptr) on that FluxDrive. Kept records of now's
// game go first (Fix 3: the new disk's fetches patch that game's own save in place during the window; a kept record put back
// later would patch its disk's sectors over them, a mixed save). Then `old` is appended: FD_PREV_KEPT. Not kept: no record,
// uid 0, another FluxDrive's, or the new disk itself (FD_PREV_NONE); the same game as now (FD_PREV_SAME_GAME) - unless
// keep_same_game (START FRESH's fling of the original: the own save is set aside and an undo puts it back, nothing is mixed).
// A record kept already (its uid) is not kept twice. A full list drops the oldest (out->evicted, out->ev).
inline int fdPrevAtFling(FdPrevList& l, const FdSaveRecord* old, const FdSaveRecord& now, bool keep_same_game, FdPrevOut* out) {
  FdPrevOut o;
  memset(&o, 0, sizeof o);
  int w = 0;
  for (int i = 0; i < l.n && i < FD_PREV_MAX; i++) {
    if (fdSameGame(l.r[i], now) || l.r[i].uid == now.uid) { o.same_dropped++; continue; }
    l.r[w++] = l.r[i];
  }
  l.n = w;
  int res = FD_PREV_NONE;
  if (old != nullptr && old->uid != 0 && old->uid != now.uid && strncmp(old->id, now.id, sizeof old->id) == 0) {
    if (!keep_same_game && fdSameGame(*old, now)) res = FD_PREV_SAME_GAME;
    else {
      res = FD_PREV_KEPT;
      bool there = false;
      for (int i = 0; i < l.n; i++) if (l.r[i].uid == old->uid) there = true;
      if (!there) {
        if (l.n >= FD_PREV_MAX) { o.evicted = true; o.ev = l.r[0]; for (int i = 1; i < l.n; i++) l.r[i - 1] = l.r[i]; l.n--; }
        l.r[l.n++] = *old;
      }
    }
  }
  if (out != nullptr) *out = o;
  return res;
}
// The list against a beacon of its FluxDrive (fdRevertCheck per kept record): Restore with *which = the kept record whose
// disk is in it again; DropPrev when cur's disk is in it and stored (no kept disk can come back: the list goes); else Keep.
inline FdRevert fdRevertList(const FdSaveRecord* cur, const FdPrevList& l, uint32_t beacon_uid, bool loaded, bool stored, int* which) {
  if (which != nullptr) *which = -1;
  for (int i = 0; i < l.n && i < FD_PREV_MAX; i++) {
    const FdRevert v = fdRevertCheck(cur, &l.r[i], beacon_uid, loaded, stored);
    if (v == FdRevert::DropPrev) return v;
    if (v == FdRevert::Restore) { if (which != nullptr) *which = i; return v; }
  }
  return FdRevert::Keep;
}

// Fix round 1: a save set aside under a name of its own, `<own without its extension>.<word>-<uid as 8 hex digits><ext>`:
// START FRESH's backup ("fresh", the fresh disk's uid) and, on an undo, the save written inside the window ("undone").
// Distinct per uid, so two START FRESHes never share a file and fdPersistLan's rebuild backup (.bak) never touches one.
// false (out "") when it does not fit whole in cap (a cut name is another file).
inline bool fdSaveAside(const char* own, const char* word, uint32_t uid, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  out[0] = 0;
  if (own == nullptr || word == nullptr) return false;
  const char* dot = strrchr(own, '.');
  const char* sl = strrchr(own, '/');
  const size_t stem = (dot != nullptr && (sl == nullptr || dot > sl)) ? (size_t)(dot - own) : strlen(own);
  const int n = snprintf(out, cap, "%.*s.%s-%08lX%s", (int)stem, own, word, (unsigned long)uid, own + stem);
  if (n < 0 || (size_t)n >= cap) { out[0] = 0; return false; }
  return true;
}

// START FRESH on a FluxDrive inside a commit window: its own save is renamed to a backup, not deleted, and this stays in RAM
// until the window closes. fresh_uid: the disk whose storing makes the start fresh final (the original START FRESH sent it);
// prior_uid: the disk it replaced, which a power cut can bring back; own: Game.sav.<tag>.adf; bak: its backup, fix round 1
// Game.sav.<tag>.fresh-<fresh uid>.adf (fdSaveAside); image: the game's file (its started-fresh mark); tag: the FluxDrive's
// save tag. undo_failed: an undo's renames failed - frozen, the backup stays where it is (fix round 1, Fix 4).
struct FdFreshPending { char id[13]; uint32_t fresh_uid, prior_uid; char own[160], bak[160], image[160]; char tag[5]; bool undo_failed; };
// Whole or not at all (p left as it was): an id (upper-case hex, 1-12), two different uids that are not 0, three SD paths
// from the root without control characters that fit whole, a save tag.
inline bool fdFreshPendingSet(FdFreshPending& p, const char* id, uint32_t fresh_uid, uint32_t prior_uid, const char* own,
                              const char* bak, const char* image, const char* tag) {
  if (id == nullptr || own == nullptr || bak == nullptr || image == nullptr || !fdSaveTagOk(tag)) return false;
  if (fresh_uid == 0 || prior_uid == 0 || fresh_uid == prior_uid) return false;
  FdFreshPending t;
  memset(&t, 0, sizeof t);
  if (strlen(id) >= sizeof t.id || strlen(own) >= sizeof t.own || strlen(bak) >= sizeof t.bak || strlen(image) >= sizeof t.image)
    return false;
  strcpy(t.id, id); strcpy(t.own, own); strcpy(t.bak, bak); strcpy(t.image, image); strcpy(t.tag, tag);
  t.fresh_uid = fresh_uid; t.prior_uid = prior_uid;
  if (!fdRecHexField(t.id, sizeof t.id, 1, sizeof t.id - 1) || !fdRecPathField(t.own, sizeof t.own) ||
      !fdRecPathField(t.bak, sizeof t.bak) || !fdRecPathField(t.image, sizeof t.image))
    return false;
  p = t;
  return true;
}
// Fix round 2: one pending entry per FluxDrive, fresh disk AND backup. START FRESH on a game with a resend and then on another
// game without one, in the same window, share the FluxDrive and the fresh disk's uid (without a resend it is the disk in it
// now) but never the backup: two entries, each undone or done with its own backup. The backup is compared as a FAT path.
inline bool fdPendSame(const FdFreshPending& a, const FdFreshPending& b) {
  return strncmp(a.id, b.id, sizeof a.id) == 0 && a.fresh_uid == b.fresh_uid && fdPathSame(a.bak, b.bak);
}
enum { FD_PEND_ADDED = 0, FD_PEND_REPLACED = 1, FD_PEND_EVICTED = 2 };
// p into the table t[0..n) that has room for cap: the same entry (fdPendSame) is replaced where it is; otherwise p is
// appended, and a full table first drops its oldest entry (*ev when ev: the caller logs it, that backup stays on the card).
// -1: no table.
inline int fdPendPut(FdFreshPending* t, int& n, int cap, const FdFreshPending& p, FdFreshPending* ev) {
  if (t == nullptr || cap <= 0) return -1;
  for (int i = 0; i < n; i++) if (fdPendSame(t[i], p)) { t[i] = p; return FD_PEND_REPLACED; }
  int res = FD_PEND_ADDED;
  if (n >= cap) { if (ev != nullptr) *ev = t[0]; for (int i = 1; i < n; i++) t[i - 1] = t[i]; n--; res = FD_PEND_EVICTED; }
  t[n++] = p;
  return res;
}
// What a beacon of that FluxDrive means for a pending START FRESH (cur_uid: its record's uid now, the disk last flung to it):
// Restore - the disk it replaced is in it again: undo, the backup is the own save again, the started-fresh mark goes;
// DropPrev - the fresh disk, or the current disk flung after it, is in it and stored: done, the backup goes (no older disk can
// come back then); Keep - anything else (an eject inside the window, a third disk), and always after a failed undo.
inline FdRevert fdFreshPendingCheck(const FdFreshPending* p, uint32_t cur_uid, uint32_t beacon_uid, bool loaded, bool stored) {
  if (p == nullptr || p->undo_failed || !loaded || beacon_uid == 0) return FdRevert::Keep;
  if (beacon_uid == p->prior_uid) return FdRevert::Restore;
  if (stored && (beacon_uid == p->fresh_uid || (cur_uid != 0 && beacon_uid == cur_uid))) return FdRevert::DropPrev;
  return FdRevert::Keep;
}

// The START FRESH confirm says "reset the Amiga afterwards" for a DOS disk (spec section 6, same-volume twins: AmigaDOS takes
// two disks with the same name and date as one volume and may keep cached files across the swap). b: the image's first n
// bytes. An AmigaDOS boot block starts "DOS" and a flags byte 0-7 (OFS, FFS, INTL, DIRCACHE and the long-name variants).
inline bool fdIsDosBoot(const uint8_t* b, size_t n) {
  return b != nullptr && n >= 4 && b[0] == 'D' && b[1] == 'O' && b[2] == 'S' && b[3] < 8;
}
