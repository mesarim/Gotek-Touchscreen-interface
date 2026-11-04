// panel_fleet.h — the JC3248 as the fleet controller: hear Webby dongle
// discovery beacons on the LAN, keep a roster, and fling the loaded disk to a
// chosen dongle over TCP. Ported from the OMEGAWARE fleet.h and adapted to
// this firmware's structures (g_disk / g_sv_img_size / g_loaded_name).
//
// Wire protocol is Webby's, unchanged (#20): UDP 51703 JSON "gti":1 beacons
// (40 s stale), TCP 3333 = 4-byte BE size + raw bytes, ack 0x01 + 4-byte LE
// load id; 0xFFFFFFFF escape → 1-byte command (0x03 eject).
//
// A FluxDrive (an ESP32-S3 board that IS drive DF0 of an Amiga, beacon "board":"fluxdrive") is a peer like a
// dongle, on the same wire. What differs for it is decided in fluxdrive_peer.h; no other board is affected.
//
// Non-blocking rule (Mez's, non-negotiable): the HTTP handler only QUEUES a
// fling/command; loop() executes it. A transfer inside a handler serves
// nothing for its whole duration, including the endpoint you would debug with.
#pragma once
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>   // #clubday: the fleet controller re-registers its own mDNS name after the panel-vs-panel election
#include "fluxdrive_peer.h"
#include <esp_wifi.h>                 // esp_wifi_get_ps / esp_wifi_set_ps (PfRadioAwake)
#include "sock_sndbuf.h"              // a socket's lwIP send buffer, read and raised in sock_sndbuf.c (lwIP stays out of here)

#define PF_DISCO_PORT 51703
#define PF_TCP_PORT   3333
#define PF_STALE_MS   40000UL
#define PF_MAX_PEERS  16
#ifndef PF_MDNS_ALIAS
#define PF_MDNS_ALIAS "gotekomega"   // the shared name: one screen ALSO answers to it (see pfApplyAlias).
#endif   // The dongles cede this exact name to a screen and their portal points users at it,
         // so it has to match theirs. A screen's own hostname is GTi-XXXX (pfDefaultHost) -
         // unique per screen, so screens never fight over it (Mez, 6 Oct 2026, option d).
#define PF_CMD_EJECT   0x03
#define PF_NO_REPLY    "no reply"   // pfSendCommand: connected, nothing came back (the wait ran out, or the peer closed)
#define PF_CMD_SETNAME 0x06   // #24: tell the dongle the pretty display name for the NEXT disk (its FAT12 root stays DISK.ADF)
// Mainline, not fleet. Webby 1.6.6 remembers which screen put the disk in and asks
// before another screen takes it over; 1.6.7 moved it here from 0x07 because that is
// ENROLL in the fleet contract. Same word "claim", a different thing - see the Registry.
#define PF_CMD_CLAIM   0x0A
// FluxDrive (v2 writing): + one byte, 1 = write protect the disk in it, 0 = writable. Answer 0x01, or 0x00 without a disk
// (pfSetWriteProtect, fdWpFailText). Never sent to any other board.
#define PF_CMD_SET_WP  0x10
#define PF_WP_WAIT_MS  5000   // FluxDrive answers after its re-insert (about 1 s); silent this long = the Amiga is reading
// A dongle claimed by a club screen answers 0x05 to anyone else. A default panel can
// still receive that byte - it just has no concept of a claim, so it reports the refusal
// as what it is to that user rather than naming a lock. Ignoring 0x05 is not an option:
// the send loop would then report a broken cable (see the comment in pfSendDisk).
#if defined(GTI_FLEET)
#define PF_REFUSED_MSG "locked to another screen"
#else
#define PF_REFUSED_MSG "dongle refused the disk"
#endif
// A FluxDrive is not a dongle: the refusal names it. A club build keeps its lock wording for everyone.
static String pfRefusedMsg(const char *board) {
#if defined(GTI_FLEET)
  (void)board;
  return String(PF_REFUSED_MSG);
#else
  return String(fdDeviceWord(board)) + " refused the disk";
#endif
}

#if defined(GTI_FLEET)   // club opcodes: the default build never sends these
#define PF_CMD_ENROLL  0x07   // #lock: enroll this panel's token as an owner (dongle's enroll window must be open)
#define PF_CMD_AUTH    0x08   // #lock: token preamble before a disk fling, proving this panel is an enrolled owner
#define PF_CMD_UNENROLL 0x09  // #lock: remove this panel's token from the dongle's owner list (unclaim/release)
#define PF_TOKEN_LEN   16     // #lock: owner-token length (bytes)
#endif

// amiga: a FluxDrive's beacon says whether the Amiga it is drive DF0 of is powered (a dongle never says; false).
// dirty, wp, tag, loadId: a FluxDrive's saves (v2 writing): the unsaved sectors of the disk it holds, its write protect,
// its save tag (Game.sav.<tag>.adf) and the disk's uid ("load_id"). Every other board: 0, false, "", 0.
// stored (fd3, the commit window): the disk in it is in flash (fdStoredField: a beacon without "stored" counts as stored,
// so every other board and an older FluxDrive: true); storedKnown: its beacon says "stored" at all.
struct PfPeer { String id, name, ip, board, fw, disk; uint16_t tcp; bool hd, loaded, amiga, lk, lkd, enr; uint32_t seen;   // #lock: lk=lock-capable, lkd=currently locked, enr=enroll window open
                uint16_t dirty; bool wp; String tag; uint32_t loadId; bool stored, storedKnown;
                // fd4, DF1: a FluxDrive's beacon says "df1":1 while its second drive is on, with loaded1, disk1, load_id1, dirty1, wp1
                // and stored1 beside the DF0 keys. Every other board, an older FluxDrive, DF1 off: df1 false and the rest empty.
                bool df1, loaded1, wp1, stored1; String disk1; uint32_t loadId1; uint16_t dirty1; };
static PfPeer   g_pfPeers[PF_MAX_PEERS];
static int      g_pfPeerN = 0;
static WiFiUDP  g_pfUdp;
static bool     g_pfUdpUp = false;

// Queue set by the web handler, drained by loop().
static String   g_pfSendIp;
static uint16_t g_pfSendTcp = PF_TCP_PORT;
static String   g_pfCmdIp;
static uint8_t  g_pfCmd = 0;
static bool     g_pfBusy = false;
static String   g_pfLastTarget, g_pfLastResult;
// Throughput of the last LAN fling, so we can compare home-WiFi vs ESP-NOW.
static uint32_t g_pfLastKbps = 0, g_pfLastBytes = 0, g_pfLastMs = 0;
// The dongle the on-screen INSERT/EJECT act on, chosen in the fleet picker.
static String   g_pfTargetName;   // set by pfWorker when a fling picks a target; NOW PLAYING renders it
#if defined(GTI_FLEET)   // this screen's owner token and the web claim queue
// #lock: this panel's owner token (identity). Generated once, persisted as PANEL_TOKEN in CONFIG.TXT
// by the sketch (loadConfig, which has SD helpers). pfSendDisk prepends it as AUTH to lock-capable dongles.
static uint8_t  g_panel_token[PF_TOKEN_LEN] = {0};
static bool     g_panel_token_ok = false;
static String   g_pfEnrollIp;   // queued by the web handler; pfWorker sends CMD_ENROLL to this dongle
// #lock: id of a dongle a WEB claim just enrolled. loop() persists DONGLE_<mac>.MINE for it
// (the sketch owns the SD helpers), so the web path records ownership like the on-screen one.
static String   g_pfUnenrollIp;  // queued by the web handler; pfWorker sends CMD_UNENROLL to this dongle
static String   g_pfClaimedId;
static String   g_pfReleasedId;   // #lock: loop() clears DONGLE_<mac>.MINE for it after a web UNCLAIM
#endif
#if defined(GTI_FLEET)   // which dongles this screen has claimed
// #lock: ids (MACs) of dongles THIS screen owns (claimed). Loaded at boot from CONFIG.TXT
// (DONGLE_<mac>.MINE=1) by the sketch; used to hide locked-not-mine dongles and to gate UNCLAIM.
static String   g_mineIds[16];
static int      g_mineN = 0;
static bool pfIsMine(const String &id){ for(int i=0;i<g_mineN;i++) if(g_mineIds[i]==id) return true; return false; }
static void pfAddMine(const String &id){ if(!id.length()||pfIsMine(id)) return; if(g_mineN<16) g_mineIds[g_mineN++]=id; }
static void pfDelMine(const String &id){ for(int i=0;i<g_mineN;i++) if(g_mineIds[i]==id){ for(int j=i;j<g_mineN-1;j++) g_mineIds[j]=g_mineIds[j+1]; g_mineN--; return; } }
#endif
// #console: a locked dongle is only visible on a screen that owns it — EXCEPT while it is in
// pairing mode (enr), so any screen can still claim a dongle whose BOOT was just tapped.
static int  pfVisibleCount();   // defined below, next to the roster
#if defined(GTI_FLEET)
static bool pfPeerVisible(const PfPeer &p){ return p.enr || pfIsMine(p.id) || !p.lkd; }
#else
// No claims exist in this build, so nothing may be hidden from the person who
// owns the hardware: every dongle we can hear is a dongle you can use.
static bool pfPeerVisible(const PfPeer &p){ (void)p; return true; }
#endif
static int  pfVisibleCount(){ int n=0; for(int i=0;i<g_pfPeerN;i++) if(pfPeerVisible(g_pfPeers[i])) n++; return n; }
static int  pfPeerIdx(const String &id){ for(int i=0;i<g_pfPeerN;i++) if(g_pfPeers[i].id==id) return i; return -1; }   // rows are held by id: a prune must not shift one under your finger
// The line under a fleet row's disk name. A FluxDrive says what it is and whether its Amiga is on;
// every other peer shows its address, with the lock state in a club build, as before.
static String pfRowInfo(const PfPeer &p) {
  String s = fdIsFluxDrive(p.board.c_str()) ? (String("FluxDrive  Amiga ") + (p.amiga ? "on" : "off")) : p.ip;
#if defined(GTI_FLEET)
  s += pfIsMine(p.id) ? "  (mine)" : (p.lkd ? "  (locked)" : "");
#endif
  return s;
}
static String pfPeerLabel(const PfPeer &p) { return p.name.length() ? p.name : p.ip; }   // how the screen names a peer
// What the fleet manager compares to know a row needs repainting besides the id list: empty for every board but a
// FluxDrive, whose Amiga state, disk, write protect (its WP key) and STORING... (fd3) change without its id changing.
static String pfRowSig(const PfPeer &p) {
  char b[96]; const char *bd = p.board.c_str();
  fdRowSig(bd, p.amiga, p.loaded, p.disk.c_str(), b, sizeof(b), p.wp, fdShowStoring(fdIsFluxDrive(bd), p.loaded, p.stored));
  return String(b);
}

// ── Panel-vs-panel election (#clubday: many screens on one Wi-Fi, one gotekomega.local) ──
// Every screen beacons role:panel with its MAC id and the mDNS name it currently holds.
// Every screen keeps its own GTi-XXXX hostname; the election only decides which one ALSO
// answers to the shared gotekomega.local: among screens on their default name, the LOWEST
// MAC. A screen given a custom MDNS_NAME never contends. Deterministic, no mDNS probing.
struct PfPanel { String id, mdns; uint32_t seen; };
static PfPanel  g_pfPanels[PF_MAX_PEERS];
static int      g_pfPanelN = 0;
static String   g_pfMyId;              // this screen's MAC (uppercase hex) — same format as the beacon id
static String   g_pfMdnsName;          // the mDNS name we WANT to be registered under (no ".local")
static bool     g_pfMdnsHeld = false;  // ... and whether the responder REPORTS that name (see pfMdnsReadback)
static char     g_pfMdnsActual[MDNS_NAME_BUF_LEN] = {0};   // what mdns_hostname_get() says we are really called
static uint32_t g_pfMdnsCheck = 0;     // last readback (throttled; the conflict rename lands after probing)
static uint32_t g_pfMdnsRetry = 0;     // last re-registration after losing the name
static uint16_t g_pfMdnsLost  = 0;     // how often we had to take it back (diagnostic)
#define PF_MDNS_RETRY_MS 20000UL       // > the window a booting dongle holds the leader name
static bool     g_pfIsLeader = true;   // do we ALSO answer to the shared gotekomega.local?
static bool     g_pfMdnsDirty = true;  // set when the elected name changes -> re-register (no reboot)
static bool     g_pfAliasUp   = false; // we currently answer to PF_MDNS_ALIAS as a delegated hostname
static uint32_t g_pfAliasIp   = 0;     // ... at this address
static uint32_t g_pfAliasTry  = 0;     // last attempt to (re)register it

static String pfMyMac() {
  uint8_t m[6]; WiFi.macAddress(m);
  char b[13]; snprintf(b, sizeof(b), "%02X%02X%02X%02X%02X%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(b);
}
static String pfMacSuffix() {   // last 2 bytes, UPPERCASE - the same as screenName()'s GTi-XXXX in espnow_server.cpp
  uint8_t m[6]; WiFi.macAddress(m);
  char b[8]; snprintf(b, sizeof(b), "%02X%02X", m[4], m[5]);
  return String(b);
}
static String pfDefaultHost() { return String("GTi-") + pfMacSuffix(); }   // a screen's hostname unless MDNS_NAME says otherwise
static String pfMdnsName() { return g_pfMdnsName.length() ? g_pfMdnsName : (g_mdns_named ? g_mdns_name : pfDefaultHost()); }
// A screen on its default name, as its beacon tells it: GTi-XXXX, or the shared name itself
// (a screen on older firmware still uses that as its hostname - it holds the alias, so it counts).
static bool pfPanelOnDefault(const String &mdns) {
  return mdns == PF_MDNS_ALIAS || (mdns.length() == 8 && mdns.startsWith("GTi-"));
}

static void pfPanelUpsert(const String &id, const String &mdns) {
  if (!id.length()) return;
  for (int i = 0; i < g_pfPanelN; i++)
    if (g_pfPanels[i].id == id) { g_pfPanels[i].mdns = mdns; g_pfPanels[i].seen = millis(); return; }
  if (g_pfPanelN < PF_MAX_PEERS) { g_pfPanels[g_pfPanelN].id = id; g_pfPanels[g_pfPanelN].mdns = mdns; g_pfPanels[g_pfPanelN].seen = millis(); g_pfPanelN++; }
}
static void pfPanelPrune() {
  uint32_t now = millis(); int w = 0;
  for (int i = 0; i < g_pfPanelN; i++)
    if (now - g_pfPanels[i].seen < PF_STALE_MS) { if (w != i) g_pfPanels[w] = g_pfPanels[i]; w++; }
  g_pfPanelN = w;
}
// Decide our effective mDNS name from the screens we can hear. Marks dirty on change.
#if defined(GTI_FLEET)
static void pfElect() {
  if (!g_pfMyId.length()) g_pfMyId = pfMyMac();
  pfPanelPrune();
  // Test the fact that MDNS_NAME was set, not whether its value differs from the
  // default: choosing the default name on purpose is still a choice.
  String want = g_mdns_named ? g_mdns_name : pfDefaultHost();
  bool lead = !g_mdns_named;                      // a named screen never contends for the shared name
  for (int i = 0; lead && i < g_pfPanelN; i++)    // yield it to any live default screen with a lower MAC
    if (pfPanelOnDefault(g_pfPanels[i].mdns) && g_pfPanels[i].id < g_pfMyId) lead = false;
  g_pfIsLeader = lead;
  if (want != g_pfMdnsName) { g_pfMdnsName = want; g_pfMdnsDirty = true; g_pfMdnsHeld = false; }
}
#else
// No election in this build: one screen, one name. Same function name on purpose -
// pfApplyMdns() is the only writer of the registration and it needs SOMEBODY to fill
// g_pfMdnsName, or the screen silently answers to no mDNS name at all.
static void pfElect() {
  if (!g_pfMyId.length()) g_pfMyId = pfMyMac();
  pfPanelPrune();                               // its only other caller is the fleet election;
                                                // without this the panel count only ever grows
  // Test that MDNS_NAME was SET, not what it says: choosing the default name on purpose
  // is still a choice. Comparing values is the bug dd61f0b fixed - do not reintroduce it.
  String want = g_mdns_named ? g_mdns_name : pfDefaultHost();
  if (want != g_pfMdnsName) { g_pfMdnsName = want; g_pfMdnsDirty = true; }
  // No election: a screen on its default name simply also answers to the shared name, so the
  // dongle portal's gotekomega.local link keeps working. Two such screens on one LAN both
  // offer it - as before this change, when it was their hostname - but their own GTi-XXXX
  // names are unique, so each one stays reachable.
  g_pfIsLeader = !g_mdns_named;
}
#endif
// Ask the responder what hostname it actually holds. begin() returning true is NOT that
// answer: it only means the responder accepted the request. If another device on the LAN
// already answers to the name, the IDF responder renames itself AFTER probing — that is
// where "gotekomega-2" came from on 22-09, a suffix our own code cannot produce. So this
// has to run periodically, not once at begin().
static void pfMdnsReadback() {
  if (millis() - g_pfMdnsCheck < 2000) return;
  g_pfMdnsCheck = millis();
  char buf[MDNS_NAME_BUF_LEN] = {0};
  if (mdns_hostname_get(buf) != ESP_OK) { g_pfMdnsActual[0] = 0; g_pfMdnsHeld = false; return; }
  strlcpy(g_pfMdnsActual, buf, sizeof(g_pfMdnsActual));
  g_pfMdnsHeld = g_pfMdnsName.length() && g_pfMdnsName == g_pfMdnsActual;
  // Lost it. A dongle claims the leader name for the first seconds of its boot (its roster is
  // still empty, so doElection makes it master) and cedes once it hears us - but by then the
  // responder has already renamed US, and pfApplyMdns only fires when the WANTED name changes.
  // So take it back, slower than that window, and only for a name we are entitled to.
  // Our hostname is unique now (GTi-XXXX, or a name the user chose), so always take it back.
  if (!g_pfMdnsHeld && g_pfMdnsName.length() && g_pfMdnsActual[0]) {
    if (millis() - g_pfMdnsRetry >= PF_MDNS_RETRY_MS) {
      g_pfMdnsRetry = millis(); g_pfMdnsDirty = true; g_pfMdnsLost++;
    }
  }
}

// Re-register mDNS when the elected name changed. Safe to call every pass — only acts when dirty.
static void pfApplyMdns() {
  if (!g_pfMdnsDirty || g_pfMdnsName.length() == 0) return;
  MDNS.end();
  g_pfAliasUp = false;                             // end() dropped the delegated shared name too
  if (!MDNS.begin(g_pfMdnsName.c_str())) return;   // stay dirty: a failed begin() must be retried,
                                                   // or the screen claims a name nothing answers to
  MDNS.addService("http", "tcp", 80);
  g_pfMdnsDirty = false;
  g_pfMdnsCheck = 0;        // force a readback on the next pass instead of claiming success here
}

// The shared gotekomega.local, answered by the leader as a delegated hostname next to its own
// GTi-XXXX. A delegated host carries its own address, so it is re-set when our IP changes.
static void pfApplyAlias() {
  const bool want = g_pfIsLeader && !g_pfMdnsDirty && g_pfMdnsName.length() && WiFi.status() == WL_CONNECTED;
  if (!want) {
    if (g_pfAliasUp) { mdns_delegate_hostname_remove(PF_MDNS_ALIAS); g_pfAliasUp = false; }
    return;
  }
  const uint32_t ip = (uint32_t)WiFi.localIP();
  if (g_pfAliasUp && ip == g_pfAliasIp) return;
  if (millis() - g_pfAliasTry < 5000) return;      // the responder may still be starting: retry, do not hammer
  g_pfAliasTry = millis();
  mdns_ip_addr_t a = {};
  a.addr.type = ESP_IPADDR_TYPE_V4; a.addr.u_addr.ip4.addr = ip; a.next = NULL;
  const esp_err_t e = g_pfAliasUp ? mdns_delegate_hostname_set_address(PF_MDNS_ALIAS, &a)
                                  : mdns_delegate_hostname_add(PF_MDNS_ALIAS, &a);
  if (e != ESP_OK) return;
  if (!g_pfAliasUp) mdns_service_add_for_host(NULL, "_http", "_tcp", PF_MDNS_ALIAS, 80, NULL, 0);
  g_pfAliasUp = true; g_pfAliasIp = ip;
}

static String pfJesc(const String &s) {
  String o; o.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c >= 0x20) o += c;
  }
  return o;
}
static String pfJf(const String &s, const char *key) {
  String k = String("\"") + key + "\":";
  int i = s.indexOf(k); if (i < 0) return "";
  i += k.length(); if (i >= (int)s.length()) return "";
  if (s[i] == '"') { int e = s.indexOf('"', i + 1); return e < 0 ? String("") : s.substring(i + 1, e); }
  int e = i; while (e < (int)s.length() && s[e] != ',' && s[e] != '}') e++;
  return s.substring(i, e);
}

static void pfPrune() {
  uint32_t now = millis(); int w = 0;
  for (int i = 0; i < g_pfPeerN; i++)
    if (now - g_pfPeers[i].seen < fdStaleMs(g_pfPeers[i].board.c_str(), PF_STALE_MS)) { if (w != i) g_pfPeers[w] = g_pfPeers[i]; w++; }
  g_pfPeerN = w;
}
static void pfUpsert(const String &j) {
  const String id = pfJf(j, "id"); if (id.length() == 0) return;
  PfPeer *p = nullptr;
  for (int i = 0; i < g_pfPeerN; i++) if (g_pfPeers[i].id == id) { p = &g_pfPeers[i]; break; }
  if (!p) { if (g_pfPeerN >= PF_MAX_PEERS) return; p = &g_pfPeers[g_pfPeerN++]; p->id = id; }
  p->name = pfJf(j, "name"); p->ip = pfJf(j, "ip"); p->board = pfJf(j, "board");
  p->fw = pfJf(j, "fw"); p->disk = pfJf(j, "disk");
  const long t = pfJf(j, "tcp").toInt(); p->tcp = (t > 0 && t < 65536) ? (uint16_t)t : PF_TCP_PORT;
  p->hd = pfJf(j, "hd") == "true"; p->loaded = pfJf(j, "loaded") == "true";
  p->amiga = pfJf(j, "amiga") == "true";
  { const long d = pfJf(j, "dirty").toInt(); p->dirty = d <= 0 ? 0 : d > 65535 ? 65535 : (uint16_t)d; }   // FluxDrive's saves
  p->wp = pfJf(j, "wp") == "true"; p->tag = pfJf(j, "tag");
  p->loadId = (uint32_t)strtoul(pfJf(j, "load_id").c_str(), nullptr, 10);   // decimal, up to 4294967295 (toInt would cut it)
  { const String st = pfJf(j, "stored"); p->storedKnown = st.length() > 0; p->stored = fdStoredField(st.c_str()); }   // fd3
  p->df1 = pfJf(j, "df1") == "1";   // fd4: DF1's keys count only while the beacon says it is on
  p->loaded1 = p->df1 && pfJf(j, "loaded1") == "true"; p->disk1 = p->df1 ? pfJf(j, "disk1") : String("");
  p->wp1 = p->df1 && pfJf(j, "wp1") == "true"; p->stored1 = !p->df1 || fdStoredField(pfJf(j, "stored1").c_str());
  p->loadId1 = p->df1 ? (uint32_t)strtoul(pfJf(j, "load_id1").c_str(), nullptr, 10) : 0;
  { const long d = p->df1 ? pfJf(j, "dirty1").toInt() : 0; p->dirty1 = d <= 0 ? 0 : d > 65535 ? 65535 : (uint16_t)d; }
#if defined(GTI_FLEET)   // a default screen neither reads nor acts on the lock keys
  p->lk = pfJf(j, "lk") == "1"; p->lkd = pfJf(j, "lkd") == "1";   // #lock: lock-capable + currently-locked
  p->enr = pfJf(j, "enr") == "1";                                 // #lock: enroll window open -> the screen offers to CLAIM
#endif
  p->seen = millis();
}

// #rule: announce this panel as the fleet LEADER on the shared discovery port, so
// the dongles cede the undecorated name to it and stay <name>-<mac>.local. A screen,
// when present, always leads. Rate-limited so it can be called every pass.
static uint32_t g_pfBeaconNext = 0;
static void pfSendBeacon() {
  if (!g_pfUdpUp || WiFi.status() != WL_CONNECTED) return;
  if (millis() < g_pfBeaconNext) return;
  g_pfBeaconNext = millis() + 8000;
  uint8_t m[6]; WiFi.macAddress(m);
  char id[13]; snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  String j = "{\"gti\":1,\"role\":\"panel\",\"id\":\"" + String(id) + "\",\"name\":\"" + pfJesc(String("GTi panel")) +
             "\",\"mdns\":\"" + pfJesc(pfMdnsName()) + "\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"loaded\":" + (g_loaded ? "true" : "false") + "}";
  g_pfUdp.beginPacket(IPAddress(255, 255, 255, 255), PF_DISCO_PORT);
  g_pfUdp.write((const uint8_t *)j.c_str(), j.length());
  g_pfUdp.endPacket();
}

// fd3, the commit window: in the sketch (next to the LAN saves). After the beacons of a pass: what they say for the record
// a fling replaced and for a pending START FRESH (fdRevertCheck, fdFreshPendingCheck). Every caller of pfService - the
// main loop, the screensavers, the fleet manager - runs it so.
static void fdCommitWatch();
// Call every loop() pass. Opens the socket once STA WiFi is up; drops it and
// the roster when the network goes away (or we are on ESP-NOW instead).
static void pfService() {
  if (WiFi.status() != WL_CONNECTED) {
    if (g_pfUdpUp) { g_pfUdp.stop(); g_pfUdpUp = false; g_pfPeerN = 0; }
    return;
  }
  if (!g_pfUdpUp) g_pfUdpUp = g_pfUdp.begin(PF_DISCO_PORT) != 0;
  if (!g_pfUdpUp) return;
  if (!g_pfMyId.length()) g_pfMyId = pfMyMac();   // needed to tell our own beacon apart from other screens
  for (int guard = 0; guard < 8; guard++) {
    const int sz = g_pfUdp.parsePacket(); if (sz <= 0) break;
    char buf[600]; const int n = g_pfUdp.read((uint8_t *)buf, sizeof(buf) - 1); if (n <= 0) break;
    buf[n] = 0; const String s(buf);
    if (s.indexOf("\"gti\":1") < 0) continue;
    if (s.indexOf("\"role\":\"panel\"") >= 0) {           // #clubday: another screen — track it for the leader election, keep it out of the dongle roster
      const String pid = pfJf(s, "id");
      if (pid.length() && pid != g_pfMyId) pfPanelUpsert(pid, pfJf(s, "mdns"));
      continue;
    }
    // #standalone: a screen serving the web UI in STANDALONE still needs the election - two screens
    // claiming <name>.local is exactly what it exists to stop - but it is NOT driving dongles, so it
    // keeps no roster. Empty roster => pfVisibleCount()==0 => disks mount on our own USB port.
    if (g_wireless_mode && g_link_home) pfUpsert(s);
  }
  fdCommitWatch();  // fd3: a FluxDrive's disk stored, or the previous one back after a power cut
  pfElect();        // #clubday: our own name, and whether we also answer gotekomega.local (lowest default MAC)
  pfApplyMdns();    // re-register if it changed — no reboot
  pfApplyAlias();   // ... add or drop the shared name to match the election
  pfMdnsReadback(); // ... and check what the responder ended up called, rather than assuming
  pfSendBeacon();   // #rule: keep announcing ourselves (with the elected name) as a leader
}

// The roster the web fleet card reads. The panel lists itself first with what
// IT has loaded, so "which device holds which disk" is answered in one view.
static String pfRosterJson() {
  pfPrune();
  const bool busy = g_pfBusy || g_pfSendIp.length() || g_pfCmdIp.length();
  String j = "{\"self\":\"panel\",\"name\":\"" + pfJesc(String("GTi panel")) + "\",";
  j += "\"mdns\":\"" + pfJesc(pfMdnsName()) + "\",";                       // the name this screen WANTS after the election
  j += "\"mdns_held\":" + String(g_pfMdnsHeld ? "true" : "false") + ",";   // ... true only when the responder REPORTS that same name
  j += "\"mdns_actual\":\"" + pfJesc(String(g_pfMdnsActual)) + "\",";       // ... and what it reports, renamed-on-conflict included
  j += "\"mdns_lost\":" + String(g_pfMdnsLost) + ",";                      // ... and how often we had to take the name back
  j += "\"leader\":" + String(g_pfIsLeader ? "true" : "false") + ",";     // #clubday: true = also answers gotekomega.local
  j += "\"alias_up\":" + String(g_pfAliasUp ? "true" : "false") + ",";   // ... and the responder accepted it
  j += "\"panels\":" + String(g_pfPanelN + 1) + ",";                      // #clubday: screens seen on the net, incl. self
  // The SPA shows the fleet card only in WIRELESS mode: STANDALONE means the
  // panel is the local drive and has no dongles to control. Home WiFi + web are
  // up in both modes, so this flag is purely a UI gate.
  j += "\"wireless\":" + String(g_wireless_mode ? "true" : "false") + ",";
  j += "\"loaded\":\"" + pfJesc(g_loaded ? g_loaded_name : String("")) + "\",";
  j += "\"busy\":" + String(busy ? "true" : "false") + ",";
  j += "\"last_target\":\"" + pfJesc(g_pfLastTarget) + "\",";
  j += "\"last_result\":\"" + pfJesc(g_pfLastResult) + "\",";
  j += "\"last_kbps\":" + String(g_pfLastKbps) + ",";
  j += "\"last_bytes\":" + String(g_pfLastBytes) + ",";
  j += "\"last_ms\":" + String(g_pfLastMs) + ",";
  int pfVis=0; for(int i=0;i<g_pfPeerN;i++) if(pfPeerVisible(g_pfPeers[i])) pfVis++;
  j += "\"count\":" + String(pfVis) + ",\"devices\":[";
  bool pfFirst=true;
  for (int i = 0; i < g_pfPeerN; i++) {
    const PfPeer &p = g_pfPeers[i];
    if (!pfPeerVisible(p)) continue;   // #console: hide locked dongles this screen does not own
    if (!pfFirst) j += ","; pfFirst=false;
    j += "{\"id\":\"" + pfJesc(p.id) + "\",\"name\":\"" + pfJesc(p.name) +
         "\",\"ip\":\"" + pfJesc(p.ip) + "\",\"board\":\"" + pfJesc(p.board) +
         "\",\"fw\":\"" + pfJesc(p.fw) + "\",\"hd\":" + (p.hd ? "true" : "false") +
         ",\"loaded\":" + (p.loaded ? "true" : "false") +
         ",\"disk\":\"" + pfJesc(p.disk) + "\",\"tcp\":" + String(p.tcp) +
         (fdIsFluxDrive(p.board.c_str()) ? String(",\"amiga\":") + (p.amiga ? "true" : "false") +
            ",\"load_id\":" + String(p.loadId) + ",\"dirty\":" + String(p.dirty) + ",\"wp\":" + (p.wp ? "true" : "false") +
            ",\"stored\":" + (p.stored ? "true" : "false") + ",\"tag\":\"" + pfJesc(p.tag) + "\"" : String("")) +
#if defined(GTI_FLEET)   // the roster drops the lock columns
         ",\"lk\":" + (p.lk ? "true" : "false") + ",\"lkd\":" + (p.lkd ? "true" : "false") +
         ",\"mine\":" + (pfIsMine(p.id) ? "true" : "false") +
#endif
         ",\"age_s\":" + String((millis() - p.seen) / 1000) + "}";
  }
  j += "]}";
  return j;
}

// #lock: send our token ONLY to a lock-capable dongle this screen actually owns. Beacons are
// unauthenticated, so "lk":1 alone must never be enough to make us hand the token to a stranger.
// #lock: locked, and not ours -> there is nothing to try. A dongle that refuses mid-stream
// answers 0x05 and closes while we are still writing, which is a TCP RST - that discards the
// byte, so the caller sees a dead connection and reports a cable fault. Answer from what the
// beacon already told us instead, and do not hand our token to somebody else's dongle.
#if defined(GTI_FLEET)   // lock-aware peer questions
static bool pfPeerLockedElsewhere(const String &ip) {
  for (int i = 0; i < g_pfPeerN; i++) if (g_pfPeers[i].ip == ip) return g_pfPeers[i].lkd && !pfIsMine(g_pfPeers[i].id);
  return false;
}
static bool pfPeerWantsAuth(const String &ip) {
  for (int i = 0; i < g_pfPeerN; i++) if (g_pfPeers[i].ip == ip) return g_pfPeers[i].lk && pfIsMine(g_pfPeers[i].id);
  return false;
}
#endif
// #lock: the TCP port this peer advertises (never assume 3333).
static uint16_t pfPeerTcp(const String &ip) {
  for (int i = 0; i < g_pfPeerN; i++) if (g_pfPeers[i].ip == ip) return g_pfPeers[i].tcp;
  return PF_TCP_PORT;
}
// The board this peer's beacon named ("" for an address the panel has not heard). Held in a String of its own:
// the roster can change while a blocking send runs.
static String pfPeerBoard(const String &ip) {
  for (int i = 0; i < g_pfPeerN; i++) if (g_pfPeers[i].ip == ip) return g_pfPeers[i].board;
  return String();
}
// A FluxDrive that stayed silent through the whole SET_NAME wait is still busy. pfSendName notes its address here and
// the pfSendDisk right behind it says "busy" at once, instead of waiting out the same game load a second time.
static FdBusyMemo g_pfBusyMemo;
#if defined(GTI_FLEET)   // the AUTH preamble
// #lock: write the AUTH preamble. Returns false if the dongle refuses us immediately (0x05).
static bool pfWriteAuth(WiFiClient &c, String &err) {
  uint8_t a[5 + PF_TOKEN_LEN]; a[0]=a[1]=a[2]=a[3]=0xFF; a[4]=PF_CMD_AUTH; memcpy(a+5, g_panel_token, PF_TOKEN_LEN);
  c.write(a, sizeof(a));
  // The dongle answers ONLY on refusal, and it does so before we send anything else — so give it a
  // short window. Without this the refusal arrives mid-payload and surfaces as "connection lost".
  const uint32_t t0 = millis();
  while (!c.available() && millis() - t0 < 400) { if (!c.connected()) break; delay(5); }
  if (c.available()) { const int r = c.read(); err = (r == 0x05) ? PF_REFUSED_MSG : "refused"; return false; }
  return true;
}
#endif

// Tell the dongle which screen is about to send it a disk (Webby 1.6.6+).
// Frame: mac[6], flags (bit0 = take over), one length byte, then the name.
// Reply: 0x01 go ahead, or 0x02 unsaved saves / 0x03 in use - both followed by a
// length byte and the name of the screen that holds it, so we can say who.
// An older dongle does not know 0x0A and answers 0x00; that is not an error, it
// just has no loader to remember. Never fatal: a refused claim stops the fling,
// an unanswered one does not.
static bool pfSendClaim(const String &ip, uint16_t port, bool takeover, String &err) {
  WiFiClient c;
  if (!c.connect(ip.c_str(), port, 5000)) { err = "connect failed"; return false; }
  c.setNoDelay(true);
  const uint8_t f[5] = { 0xFF, 0xFF, 0xFF, 0xFF, PF_CMD_CLAIM };
  c.write(f, 5);
  uint8_t mac[6]; WiFi.macAddress(mac);
  c.write(mac, 6);
  c.write((uint8_t)(takeover ? 1 : 0));
  String nm = pfMdnsName(); if (nm.length() > 24) nm = nm.substring(0, 24);
  c.write((uint8_t)nm.length());
  if (nm.length()) c.write((const uint8_t *)nm.c_str(), nm.length());
  const uint32_t t0 = millis();
  while (c.available() < 1 && millis() - t0 < 3000) { if (!c.connected()) break; delay(5); }
  const int a = c.available() >= 1 ? c.read() : -1;
  if (a == 0x01 || a < 0 || a == 0x00) { c.stop(); return true; }   // go ahead, or a dongle that predates 0x0A
  // refused: read the length-prefixed name of whoever holds it
  String who;
  const uint32_t t1 = millis();
  while (c.available() < 1 && millis() - t1 < 1000) { if (!c.connected()) break; delay(5); }
  int wl = c.available() >= 1 ? c.read() : 0;
  for (int i = 0; i < wl && millis() - t1 < 2000; ) {
    if (!c.connected() && !c.available()) break;
    int ch = c.read();
    if (ch < 0) { delay(1); continue; }
    who += (char)ch; i++;
  }
  c.stop();
  if (!who.length()) who = "another screen";
  // 0x02 = its saves are not handed back yet; 0x03 = in use (every board has saves since FluxDrive v2, fdHasSaves; a
  // FluxDrive never answers 0x02).
  err = (a == 0x02 && fdHasSaves(pfPeerBoard(ip).c_str())) ? ("unsaved saves on " + who) : ("in use by " + who);
  return false;
}

// Push the loaded disk to a dongle. BLOCKING — loop() only.
static void pfNoteTarget(const String &ip) {   // NOW PLAYING shows where the disk went
  g_pfTargetName = ip;
  for (int i = 0; i < g_pfPeerN; i++) if (g_pfPeers[i].ip == ip && g_pfPeers[i].name.length()) { g_pfTargetName = g_pfPeers[i].name; return; }
}
// The main screen speaks for one FluxDrive in fleet mode (fdPickTarget): where the last disk of this boot went, else the
// only one heard. Its beacon ("loaded", "disk") is what NOW PLAYING, the loaded marker and EJECT go by.
static String g_pfLastFlingId;   // beacon id of the peer the last acked disk went to ("" = none yet this boot)
// The beacons queued while a send or an eject blocked loop() were sent before the ack: the old disk, the empty drive of
// FluxDrive's 2-s swap. Read after the roster was set from the ack, the last of them would win. Drop them; the beacons
// that follow the ack rule (FluxDrive sends one right after a mount or an eject).
static void pfDropQueuedBeacons() {
  if (!g_pfUdpUp) return;
  g_pfUdp.flush();   // a datagram still held by the socket object blocks parsePacket()
  for (int g = 0; g < 32 && g_pfUdp.parsePacket() > 0; g++) g_pfUdp.flush();
}
// A disk went to ip and was acked (FluxDrive acks after the mount). A FluxDrive holds it from now on, under the name it
// was sent with ("DISK.ADF" = "DISK" when none was), so the screen does not show the old disk until the next beacon.
// Its uid is the ack's (0 = not read) and nothing of it is unsaved yet: a beacon queued before the ack (the old disk's
// dirty count) must not start a fetch. fd3: nor is it in flash yet (its commit starts now) when its beacon speaks of
// "stored" at all: the old disk's "stored" must not read as the new disk's (fdCommitWatch would forget the replaced record).
static void pfNoteFling(const String &ip, const String &sent, uint32_t uid, int drive = 0) {
  for (int i = 0; i < g_pfPeerN; i++) { PfPeer &p = g_pfPeers[i]; if (p.ip != ip) continue;
    g_pfLastFlingId = p.id;
    if (drive == 1 && fdIsFluxDrive(p.board.c_str())) { p.loaded1 = true; p.disk1 = sent.length() ? sent : String("DISK"); p.dirty1 = 0; p.loadId1 = uid;   // fd4: DF1 alike
      if (p.storedKnown) p.stored1 = false;
      pfDropQueuedBeacons(); return; }
    if (fdIsFluxDrive(p.board.c_str())) { p.loaded = true; p.disk = sent.length() ? sent : String("DISK"); p.dirty = 0; p.loadId = uid;
      if (p.storedKnown) p.stored = false;
      pfDropQueuedBeacons(); }
    return; }
}
// An eject was acked: a FluxDrive is empty from now on (its next beacon says so too), with nothing it can still hand
// over (an ejected disk's unsaved sectors cannot be fetched). Any other board: nothing changes.
static void pfNoteEject(const String &ip, int drive = 0) {
  for (int i = 0; i < g_pfPeerN; i++) if (g_pfPeers[i].ip == ip && fdIsFluxDrive(g_pfPeers[i].board.c_str())) {
    if (drive == 1) { g_pfPeers[i].loaded1 = false; g_pfPeers[i].disk1 = ""; g_pfPeers[i].dirty1 = 0; pfDropQueuedBeacons(); return; }   // fd4
    g_pfPeers[i].loaded = false; g_pfPeers[i].disk = ""; g_pfPeers[i].dirty = 0; pfDropQueuedBeacons(); return; }
}
// The FluxDrive target as an index into g_pfPeers, -1 = none or not in fleet mode (WIRELESS + WiFi, home Wi-Fi up).
// Heard = shown here and not silent past its stale limit (the roster itself is pruned only now and then).
static int pfFluxTarget() {
  if (!(g_wireless_mode && g_link_home && WiFi.status() == WL_CONNECTED)) return -1;
  bool vis[PF_MAX_PEERS], fd[PF_MAX_PEERS]; const char *ids[PF_MAX_PEERS]; const uint32_t now = millis();
  for (int i = 0; i < g_pfPeerN; i++) { const PfPeer &p = g_pfPeers[i]; const char *bd = p.board.c_str();
    vis[i] = pfPeerVisible(p) && fdHeard(now, p.seen, fdStaleMs(bd, PF_STALE_MS)); fd[i] = fdIsFluxDrive(bd); ids[i] = p.id.c_str(); }
  return fdPickTarget(g_pfPeerN, vis, fd, ids, g_pfLastFlingId.c_str());
}
// ── Speed: a push to a FluxDrive with up to 15 segments in flight instead of 4 ──
// lwIP's send buffer is TCP_SND_BUF = 5744 B (4 segments), compiled into the core's liblwip.a, with no per-socket option
// for it. With 60-100 ms round trips that caps a push at about 58 kB/s, while a FluxDrive's receive window takes 16
// segments. pfRaiseSndBuf raises this one connection's snd_buf after connect and before the first byte, through
// sock_sndbuf.c (lwIP's own state, under the tcpip core lock). fdSendBufBytes says how much: one pbuf short of
// TCP_SND_QUEUELEN, so 15 x 1436 = 21,540 B in this build. fdSndBufDelta says whether at all (a TCP socket, ESTABLISHED,
// to the push's port, still at lwIP's default) and adds only the difference. lwIP adds acked bytes back without a cap at
// TCP_SND_BUF, so the raise holds for this connection and ends with it. CONFIG.TXT FD_SNDBUF=OFF, read at every push,
// leaves lwIP's default for an A/B measurement; the sketch's POST /api/fd/sndbuf?on=0|1 flips it, no reflash, no restart.
struct PfSndBufNote { uint32_t was = 0, now = 0, mss = 0; const char *why = ""; };   // what pfRaiseSndBuf did, for the log line
static void pfRaiseSndBuf(WiFiClient &c, const char *board, uint16_t port, PfSndBufNote &n) {
  SockTcpInfo t; sock_tcp_info(c.fd(), &t);
  n.was = t.snd_buf; n.mss = t.mss;
  const uint32_t delta = fdSndBufDelta(fdSendBufBytes(board, t.mss, t.queuelen), t.found, t.established, t.remote_port, port,
                                       t.snd_buf, t.snd_buf_default, &n.why);
  if (delta) { n.now = sock_add_sndbuf(c.fd(), port, t.snd_buf, delta); if (!n.now) n.why = "changed while it was raised"; }
}
// An on/off key of CONFIG.TXT, read from the card now (fdCfgSwitch; the last line for the key wins, as in loadConfig).
static bool pfCfgSwitchOn(const char *key, bool dflt) {
  File f = SD_MMC.open("/CONFIG.TXT", FILE_READ); if (!f) return dflt;
  bool on = dflt;
  while (f.available()) { const String l = f.readStringUntil('\n'); const int sw = fdCfgSwitch(l.c_str(), key); if (sw >= 0) on = (sw == 1); }
  f.close(); return on;
}
// A push to a FluxDrive runs with the radio fully awake. In fleet mode the panel already runs without power save
// (webPanelBegin: WiFi.setSleep(false), applied again at every STA start); this asks the driver itself and, if modem
// sleep is on after all, lifts it for the push and puts it back after (as DavRadioWake in webdav_client.h does).
struct PfRadioAwake {
  wifi_ps_type_t prev = WIFI_PS_NONE;
  bool known = false, lifted = false;
  explicit PfRadioAwake(bool want) {
    if (!want) return;
    known = esp_wifi_get_ps(&prev) == ESP_OK;
    if (known && prev != WIFI_PS_NONE) lifted = esp_wifi_set_ps(WIFI_PS_NONE) == ESP_OK;
  }
  ~PfRadioAwake() { if (lifted) esp_wifi_set_ps(prev); }
  const char *was() const { return !known ? "unknown" : prev == WIFI_PS_NONE ? "off" : prev == WIFI_PS_MIN_MODEM ? "min-modem" : "max-modem"; }
};
// loadId (optional): a FluxDrive's ack carries the disk's uid (0x01 + LE32), the key of the panel's save record; 0 for any
// other board (its ack's load id is not read, as before) or when it did not come.
// drive (fd4): 1 = the disk went to a FluxDrive's DF1 (SET_DRIVE 1 was acked just before: pfSendDrive); the roster notes it there.
static bool pfSendDisk(const String &ip, uint16_t port, String &err, uint32_t *loadId = nullptr, int drive = 0) {
  if (loadId) *loadId = 0;
#if defined(GTI_FLEET)
  if (pfPeerLockedElsewhere(ip)) { err = "locked to another screen"; return false; }
#endif
  // What the beacon says this address is. Only a FluxDrive changes anything below.
  const String board = pfPeerBoard(ip);
  const char *bd = board.c_str();
  PfRadioAwake awake(fdIsFluxDrive(bd));   // any other board: nothing is asked or changed
  const bool sbOn = fdIsFluxDrive(bd) && pfCfgSwitchOn("FD_SNDBUF", true);   // read before connecting: no card read in front of the header
  // A FluxDrive takes one thing, a DD ADF of 901,120 bytes. Say so before any connection, so nothing is
  // claimed, named or sent for a disk it would only refuse.
  if (fdIsFluxDrive(bd)) {
    if (!g_loaded || g_img_bytes == 0) { err = "no disk loaded"; return false; }
    const char *refuse = fdRefuseImage(bd, g_img_bytes);
    if (refuse) { err = refuse; return false; }
    // SET_NAME just ran out against this FluxDrive: it is still reading for the Amiga. Say so now; waiting again
    // would keep the person at the screen another two minutes for the same answer.
    if (g_pfBusyMemo.take(ip.c_str(), millis(), FD_BUSY_MEMO_MS)) { err = fdBusyText(); return false; }
  }
  // Mainline: say who is sending before sending. A refusal here is the dongle
  // protecting somebody else's disk or unsaved saves, so it stops the fling.
  if (!pfSendClaim(ip, port, false, err)) return false;
  if (!g_loaded || g_img_bytes == 0) { err = "no disk loaded"; return false; }
  const uint8_t *data = g_disk + DATA_LBA * 512;
  const uint32_t size = g_img_bytes;
  WiFiClient c;
  if (!c.connect(ip.c_str(), port, 8000)) { err = "connect failed"; return false; }
  c.setNoDelay(true);   // disable Nagle: with the dongle's delayed-ACK, Nagle stalls each window ~200ms and crawls the fling to ~35 KB/s
  PfSndBufNote sb;   // FluxDrive: up to 15 segments in flight instead of 4 (pfRaiseSndBuf), unless FD_SNDBUF=OFF
  if (fdIsFluxDrive(bd)) { if (sbOn) pfRaiseSndBuf(c, bd, port, sb); else sb.why = "FD_SNDBUF=OFF"; }
#if defined(GTI_FLEET)
  if (g_panel_token_ok && pfPeerWantsAuth(ip)) {   // #lock: prove ownership before the disk
    if (!pfWriteAuth(c, err)) { c.stop(); return false; }
  }
#endif
  const uint8_t hdr[4] = { (uint8_t)(size >> 24), (uint8_t)(size >> 16), (uint8_t)(size >> 8), (uint8_t)size };
  const uint32_t tSend0 = millis();   // time the bulk transfer for the throughput report
  c.write(hdr, 4);
  uint32_t sent = 0, stall = millis();
  const uint32_t deadline = millis() + 180000UL;   // hard ceiling: a crawling link must not hold the screen hostage for minutes
  while (sent < size) {
    // A refusal arrives mid-stream: the dongle answers 0x05 and hangs up while we are still
    // writing. Read it, or the caller reports a broken cable when it was actually a locked dongle.
    if (c.available()) { const int r = c.read(); c.stop();
      err = (r == 0x05) ? pfRefusedMsg(bd) : (r == 0x00) ? (String(fdDeviceWord(bd)) + " refused the disk") : ("refused (0x" + String(r, HEX) + ")");
      return false; }
    if (!c.connected()) {
      if (c.available()) { const int r = c.read(); if (r == 0x05) { err = pfRefusedMsg(bd); c.stop(); return false; } }
      err = "connection lost at " + String(sent) + "B"; c.stop(); return false; }
    if ((int32_t)(millis() - deadline) > 0) { err = "too slow - " + String(sent*100/size) + "% in 3 min"; c.stop(); return false; }
    uint32_t chunk = size - sent; if (chunk > 8192) chunk = 8192;
    const size_t w = c.write(data + sent, chunk);
    // A FluxDrive does not read while the Amiga's motor runs: the transfer stalls until the load is over.
    if (w == 0) { if (millis() - stall > fdStallLimitMs(bd)) { err = fdIsFluxDrive(bd) ? fdBusyText() : "send stalled"; c.stop(); return false; } delay(2); continue; }
    sent += w; stall = millis();
  }
  pfNoteTarget(ip);
  g_pfLastMs = millis() - tSend0; g_pfLastBytes = size;
  g_pfLastKbps = g_pfLastMs ? (uint32_t)((uint64_t)size * 1000ULL / g_pfLastMs / 1024ULL) : 0;
  const uint32_t t0 = millis();
  const uint32_t ackWait = fdReplyWaitMs(bd, 10000);
  while (c.available() < 1 && millis() - t0 < ackWait) { if (!c.connected()) break; delay(5); }
  const int a = c.available() >= 1 ? c.read() : -1;
  uint32_t uid = 0;   // a FluxDrive sends the uid with the 0x01, in the same write; up to 1 s for the 4 bytes
  if (a == 0x01 && fdIsFluxDrive(bd)) {
    uint8_t u[4]; int got = 0; const uint32_t t1 = millis();
    while (got < 4 && millis() - t1 < 1000) { const int ch = c.read(); if (ch < 0) { if (!c.connected() && !c.available()) break; delay(2); continue; } u[got++] = (uint8_t)ch; }
    if (got == 4) uid = (uint32_t)u[0] | ((uint32_t)u[1] << 8) | ((uint32_t)u[2] << 16) | ((uint32_t)u[3] << 24);
  }
  c.stop();
  if (fdIsFluxDrive(bd)) {   // one gti.log line per push, after it: what it took, what the send buffer was, the link
    char sbt[96];
    if (sb.now) snprintf(sbt, sizeof sbt, "send buffer %u B (raised from %u, mss %u)", (unsigned)sb.now, (unsigned)sb.was, (unsigned)sb.mss);
    else snprintf(sbt, sizeof sbt, "send buffer left as it was: %s", sb.why);
    gLog("[fleet] push to FluxDrive %s: %u B in %u ms = %u kB/s to the last byte queued, ack %s after %u ms more; %s, power save %s%s, rssi %d\n",
         ip.c_str(), (unsigned)size, (unsigned)g_pfLastMs, (unsigned)(g_pfLastMs ? size / g_pfLastMs : 0), a == 0x01 ? "ok" : a < 0 ? "none" : "refused",
         (unsigned)(millis() - t0), sbt, awake.was(), awake.lifted ? " (lifted for the push)" : "", (int)WiFi.RSSI());
  }
  if (a != 0x01) { err = (a==0x05) ? pfRefusedMsg(bd) : (a < 0) ? "no ack" : (String(fdDeviceWord(bd)) + " refused"); return false; }   // #lock: 0x05 = the dongle rejected us (not an owner)
  pfNoteFling(ip, g_loaded_display.length() ? g_loaded_display : g_loaded_name, uid, drive);   // the name every caller sent with SET_NAME
  if (loadId) *loadId = uid;
  return true;
}

// ranOut (optional): the reply wait ran out with nothing read, not an early close (fdReplyRanOut). A FluxDrive holds a
// command while the Amiga reads, so for it that means "busy, it runs later".
// drive (fd4): 1 with PF_CMD_EJECT = EJECT_DRIVE 1 (DF1 of a FluxDrive); any other command, or 0: as before.
static bool pfSendCommand(const String &ip, uint16_t port, uint8_t cmd, String &err, bool *ranOut = nullptr, int drive = 0) {
  if (ranOut) *ranOut = false;
#if defined(GTI_FLEET)
  if (pfPeerLockedElsewhere(ip)) { err = "locked to another screen"; return false; }
#endif
  WiFiClient c;
  if (!c.connect(ip.c_str(), port, 5000)) { err = "connect failed"; return false; }
  c.setNoDelay(true);
#if defined(GTI_FLEET)
  if (g_panel_token_ok && pfPeerWantsAuth(ip)) {   // #lock: commands are privileged too (eject/save/name)
    if (!pfWriteAuth(c, err)) { c.stop(); return false; }
  }
#endif
  const bool df1Eject = drive == 1 && cmd == PF_CMD_EJECT;
  uint8_t f[6] = { 0xFF, 0xFF, 0xFF, 0xFF, cmd, 0 };
  size_t fn = 5;
  if (df1Eject) fn = fdEjectFrame(1, f);
  c.write(f, fn);
  const uint32_t t0 = millis();
  while (c.available() < 1 && millis() - t0 < 3000) { if (!c.connected()) break; delay(5); }
  const int a = c.available() >= 1 ? c.read() : -1;
  if (ranOut) *ranOut = fdReplyRanOut(a >= 0, millis() - t0, 3000);
  c.stop();
  // Only 0x01 is success. The dongle answers 0x02 for "refused, unsaved data", 0x05 for "locked".
  if (a == 0x01) { if (cmd == PF_CMD_EJECT) pfNoteEject(ip, df1Eject ? 1 : 0); return true; }
  err = (a < 0) ? PF_NO_REPLY : (a == 0x02 && fdHasSaves(pfPeerBoard(ip).c_str())) ? "refused - unsaved data (use force)" : (a == 0x05) ? PF_REFUSED_MSG : "refused";
  return false;
}

// Write protect on the disk in FluxDrive `peer` (an index into g_pfPeers), v2 writing. BLOCKING, loop() only. FluxDrive
// makes the change a re-insert (the drive empty for 1 s, then the disk back: AmigaDOS reads the tab when a disk comes in)
// and answers after it. While the Amiga reads it holds the command and makes the change after the load: the wait here
// runs out first (err FD_WP_BUSY) and the beacon's "wp" shows the change when it is made - the beacon is what the screen
// goes by. An 0x01 sets the roster's "wp" at once and drops the beacons queued meanwhile (sent before the answer: the old
// state, the empty drive of the re-insert); the next beacon says it too. Any other board: nothing is sent.
static bool pfSetWriteProtect(int peer, bool on, String &err) {
  if (peer < 0 || peer >= g_pfPeerN || !fdIsFluxDrive(g_pfPeers[peer].board.c_str())) { err = "not a FluxDrive"; return false; }
  const String ip = g_pfPeers[peer].ip, id = g_pfPeers[peer].id, tag = g_pfPeers[peer].tag;   // copies: the roster may change meanwhile
  const uint16_t port = g_pfPeers[peer].tcp;
#if defined(GTI_FLEET)
  if (pfPeerLockedElsewhere(ip)) { err = "locked to another screen"; return false; }
#endif
  WiFiClient c;
  if (!c.connect(ip.c_str(), port, 5000)) { err = "connect failed"; gLog("[fleet] write protect %s on FluxDrive %s: %s\n", on ? "on" : "off", ip.c_str(), err.c_str()); return false; }
  c.setNoDelay(true);
#if defined(GTI_FLEET)
  if (g_panel_token_ok && pfPeerWantsAuth(ip)) {   // #lock: gated like every other command
    if (!pfWriteAuth(c, err)) { c.stop(); return false; }
  }
#endif
  const uint8_t f[6] = { 0xFF, 0xFF, 0xFF, 0xFF, PF_CMD_SET_WP, (uint8_t)(on ? 1 : 0) };
  c.write(f, sizeof f);
  const uint32_t t0 = millis();
  while (c.available() < 1 && millis() - t0 < PF_WP_WAIT_MS) { if (!c.connected()) break; delay(5); }
  const int a = c.available() >= 1 ? c.read() : -1;
  const uint32_t waited = millis() - t0;
  c.stop();
  const char *why = fdWpFailText(a, fdReplyRanOut(a >= 0, waited, PF_WP_WAIT_MS));
  gLog("[fleet] write protect %s on FluxDrive %s (%s): %s after %u ms\n", on ? "on" : "off", tag.c_str(), ip.c_str(), why ? why : "done", (unsigned)waited);
  if (why) { err = why; return false; }
  const int k = pfPeerIdx(id); if (k >= 0) g_pfPeers[k].wp = on;
  pfDropQueuedBeacons();
  return true;
}

// #24: set-next-name — tell the dongle the pretty display name before the disk.
// Best-effort: an old dongle ignores the unknown escape (one-sided safe), so a
// failure here never blocks the fling.
static bool pfSendName(const String &ip, uint16_t port, const String &name, String &err) {
#if defined(GTI_FLEET)
  if (pfPeerLockedElsewhere(ip)) { err = "locked to another screen"; return false; }
#endif
  // A FluxDrive that will refuse the disk gets no name: the answer would only wait out a game load,
  // and the push that follows says why.
  const String board = pfPeerBoard(ip);
  const char *bd = board.c_str();
  if (g_img_bytes != 0) { const char *refuse = fdRefuseImage(bd, g_img_bytes); if (refuse) { err = refuse; return false; } }
  if (fdIsFluxDrive(bd)) g_pfBusyMemo.clear();
  WiFiClient c;
  if (!c.connect(ip.c_str(), port, 4000)) { err = "connect failed"; return false; }
  c.setNoDelay(true);
#if defined(GTI_FLEET)
  if (g_panel_token_ok && pfPeerWantsAuth(ip)) {   // #lock: SET_NAME is privileged too - without this a locked dongle
    if (!pfWriteAuth(c, err)) { c.stop(); return false; }   // refuses the name and the fling lands as "DISK.ADF"
  }
#endif
  String nm = name; if (nm.length() > 120) nm = nm.substring(0, 120);
  const uint8_t hdr[6] = { 0xFF, 0xFF, 0xFF, 0xFF, PF_CMD_SETNAME, (uint8_t)nm.length() };
  c.write(hdr, 6);
  if (nm.length()) c.write((const uint8_t *)nm.c_str(), nm.length());
  const uint32_t t0 = millis();
  const uint32_t nameWait = fdReplyWaitMs(bd, 3000);
  while (c.available() < 1 && millis() - t0 < nameWait) { if (!c.connected()) break; delay(3); }
  const int a = c.available() >= 1 ? c.read() : -1;
  const bool waitRanOut = (millis() - t0 >= nameWait);
  c.stop();
  if (fdNameMeansBusy(bd, a >= 0, waitRanOut)) g_pfBusyMemo.note(ip.c_str(), millis());
  if (a != 0x01) { err = (a < 0) ? "no ack" : "not supported"; return false; }
  return true;
}

// fd4: SET_DRIVE for the NEXT push and CLAIM from this panel (FluxDrive keeps it with the sender's address, one-shot, 150 s).
// Sent just before a push to DF1, after SET_NAME. A push goes on only when this is acked (0x01): without it the disk would land
// in DF0 (fdSetDriveFail). 0x00 = DF1 went off meanwhile. FluxDrive holds the reply while the Amiga reads, so it waits as SET_NAME
// does; when SET_NAME ran out against this address (g_pfBusyMemo) it says busy at once. Never sent to any other board.
// quick: a clear-up (SET_DRIVE 0 after a failed DF1 push): waits 1.5 s only, no busy memo, the answer is of no matter.
static bool pfSendDrive(const String &ip, uint16_t port, int drive, String &err, bool quick = false) {
#if defined(GTI_FLEET)
  if (pfPeerLockedElsewhere(ip)) { err = "locked to another screen"; return false; }
#endif
  const String board = pfPeerBoard(ip);
  const char *bd = board.c_str();
  if (!fdIsFluxDrive(bd)) { err = "not a FluxDrive"; return false; }
  if (!quick && g_pfBusyMemo.take(ip.c_str(), millis(), FD_BUSY_MEMO_MS)) { err = fdBusyText(); return false; }
  WiFiClient c;
  if (!c.connect(ip.c_str(), port, 4000)) { err = "connect failed"; return false; }
  c.setNoDelay(true);
#if defined(GTI_FLEET)
  if (g_panel_token_ok && pfPeerWantsAuth(ip)) {
    if (!pfWriteAuth(c, err)) { c.stop(); return false; }
  }
#endif
  uint8_t f[6];
  const size_t fn = fdDriveFrame(FD_CMD_SET_DRIVE, drive, f);
  c.write(f, fn);
  const uint32_t t0 = millis();
  const uint32_t wait = quick ? 1500u : fdReplyWaitMs(bd, 3000);
  while (c.available() < 1 && millis() - t0 < wait) { if (!c.connected()) break; delay(3); }
  const int a = c.available() >= 1 ? c.read() : -1;
  const bool ranOut = fdReplyRanOut(a >= 0, millis() - t0, wait);
  c.stop();
  const char *why = fdSetDriveFail(a, ranOut);
  if (why) { err = why; return false; }
  return true;
}

#if defined(GTI_FLEET)   // claim
// #lock: enroll this panel's token as an owner of the dongle at ip. The dongle's enroll
// window must be open (short BOOT tap on the dongle). Short best-effort exchange.
static bool pfSendEnroll(const String &ip, uint16_t port, String &err) {
  if (!g_panel_token_ok) { err = "no panel token (SD missing?)"; return false; }   // #lock: never enrol an all-zero token
  WiFiClient c;
  if (!c.connect(ip.c_str(), port, 4000)) { err = "connect failed"; return false; }
  c.setNoDelay(true);
  uint8_t a[5 + PF_TOKEN_LEN]; a[0]=a[1]=a[2]=a[3]=0xFF; a[4]=PF_CMD_ENROLL; memcpy(a+5, g_panel_token, PF_TOKEN_LEN);
  c.write(a, sizeof(a));
  const uint32_t t0 = millis();
  while (c.available() < 1 && millis()-t0 < 4000) { if(!c.connected()) break; delay(3); }
  const int r = c.available() >= 1 ? c.read() : -1;
  c.stop();
  if (r == 0x01) return true;
  err = (r==0x02) ? "open the dongle first (tap BOOT)" : (r==0x03) ? "dongle owners full" : (r<0) ? "no reply" : "refused";
  return false;
}
#endif

// #lock: release ownership — tell the dongle to drop THIS panel's token. Blocking, short.
// UNENROLL sits below the dongle's AUTH gate, so on the shared LAN it needs the AUTH preamble on
#if defined(GTI_FLEET)   // release
// this same connection. Always sent, not via pfPeerWantsAuth(): RELEASE LOCKS aims at dongles we
// no longer list as ours, and the UNENROLL frame carries the same token anyway.
static bool pfSendUnenroll(const String &ip, uint16_t port, String &err) {
  if (!g_panel_token_ok) { err = "no panel token (SD missing?)"; return false; }
  WiFiClient c;
  if (!c.connect(ip.c_str(), port, 4000)) { err = "connect failed"; return false; }
  c.setNoDelay(true);
  if (!pfWriteAuth(c, err)) { c.stop(); return false; }   // 0x05 = not one of its owners; 0x00 = no fleet layer
  uint8_t a[5 + PF_TOKEN_LEN]; a[0]=a[1]=a[2]=a[3]=0xFF; a[4]=PF_CMD_UNENROLL; memcpy(a+5, g_panel_token, PF_TOKEN_LEN);
  c.write(a, sizeof(a));
  const uint32_t t0 = millis();
  while (c.available() < 1 && millis()-t0 < 4000) { if(!c.connected()) break; delay(3); }
  const int r = c.available() >= 1 ? c.read() : -1;
  c.stop();
  if (r == 0x01) return true;
  err = (r<0) ? "no reply" : "refused"; return false;
}
#endif

// ── LAN saves (v2 writing): what the last single-target fling put in each FluxDrive ──
// One record per FluxDrive id (fluxdrive_peer.h FdSaveRecord), in RAM and in /GTI/fdsaves.txt (loaded at boot, written
// again after every change), so that a save fetched after either side restarted still lands in the right
// Game.sav.<tag>.adf. The fetch itself and the fling's bookkeeping live in the sketch, next to the ESP-NOW save fetch
// (svPatchCore and the save names are there).
#define PF_FDSAVES     "/GTI/fdsaves.txt"
#define PF_FDSAVES_MAX 8
static FdSaveRecord g_pfRecs[PF_FDSAVES_MAX];
static int          g_pfRecN = 0;
static FdSaveRecord *pfRecordFor(const String &id) {
  for (int i = 0; i < g_pfRecN; i++) if (id == g_pfRecs[i].id) return &g_pfRecs[i];
  return nullptr;
}// fd4: the record key of a FluxDrive's drive: its id for DF0 (the key the table always had), "<id>:1" for DF1.
static String pfRecKey(const PfPeer &p, int drive) { char k[24]; return fdRecKey(p.id.c_str(), drive, k, sizeof k) ? String(k) : String(""); }
static FdSaveRecord *pfRecordForDrive(const PfPeer &p, int drive) { return pfRecordFor(pfRecKey(p, drive)); }
// The whole table to the card: a .tmp first, which then replaces the file (a power cut leaves the old file, or the new
// .tmp that pfRecordsLoad takes). False = the card still has the table as it was.
static bool pfRecordsWrite() {
  const String tmp = String(PF_FDSAVES) + ".tmp";
  SD_MMC.remove(tmp);
  File f = SD_MMC.open(tmp, FILE_WRITE); if (!f) return false;
  bool ok = true; char line[400];
  for (int i = 0; i < g_pfRecN && ok; i++) {
    const int n = fdRecordFormat(g_pfRecs[i], line, sizeof line - 1); if (n < 0) continue;
    line[n] = '\n';
    ok = f.write((const uint8_t *)line, n + 1) == (size_t)(n + 1);
  }
  f.close();
  if (!ok) { SD_MMC.remove(tmp); return false; }
  SD_MMC.remove(PF_FDSAVES);
  return SD_MMC.rename(tmp, PF_FDSAVES);
}
static void pfRecordsLoad() {
  g_pfRecN = 0;
  const String tmp = String(PF_FDSAVES) + ".tmp";
  if (!SD_MMC.exists(PF_FDSAVES) && SD_MMC.exists(tmp)) SD_MMC.rename(tmp, PF_FDSAVES);   // cut between the remove and the rename
  File f = SD_MMC.open(PF_FDSAVES, FILE_READ); if (!f) return;
  while (f.available()) {
    const String l = f.readStringUntil('\n'); FdSaveRecord r;
    if (!fdRecordParse(l.c_str(), r)) continue;   // a line that is not a record is skipped (and gone after the next write)
    FdSaveRecord *e = pfRecordFor(String(r.id));
    if (!e) { if (g_pfRecN >= PF_FDSAVES_MAX) continue; e = &g_pfRecs[g_pfRecN++]; }
    *e = r;
  }
  f.close();
}
// r becomes the record of its FluxDrive (the one before goes), at the end of the table; a full table drops the
// FluxDrive flung to longest ago. Written to the card at once; false = only RAM has it.
static bool pfRecordPut(const FdSaveRecord &r) {
  int k = -1;
  for (int i = 0; i < g_pfRecN; i++) if (strcmp(g_pfRecs[i].id, r.id) == 0) { k = i; break; }
  if (k < 0 && g_pfRecN >= PF_FDSAVES_MAX) k = 0;
  if (k >= 0) { for (int i = k + 1; i < g_pfRecN; i++) g_pfRecs[i - 1] = g_pfRecs[i]; g_pfRecN--; }
  g_pfRecs[g_pfRecN++] = r;
  return pfRecordsWrite();
}
// ── fd2 fix round 1: FluxDrives STARTED FRESH on a game (fluxdrive_peer.h FdFreshSet) ──
// Until such a FluxDrive has an own save of that game again, a single-target fling of it sends the original image, not the
// cable save (fdFileToSend's `reset`). In RAM and in /GTI/fdfresh.txt, loaded at boot and written whole after every change
// (a .tmp first, as the save records), so a restart of the panel keeps it.
#define PF_FDFRESH "/GTI/fdfresh.txt"
static FdFreshSet g_pfFresh;   // zeroed: no marks
static bool pfFreshWrite() {
  const String tmp = String(PF_FDFRESH) + ".tmp";
  SD_MMC.remove(tmp);
  File f = SD_MMC.open(tmp, FILE_WRITE); if (!f) return false;
  bool ok = true; char line[200];
  for (int i = 0; i < g_pfFresh.n && ok; i++) {
    const int n = fdFreshFormat(g_pfFresh.m[i], line, sizeof line - 1); if (n < 0) continue;
    line[n] = '\n';
    ok = f.write((const uint8_t *)line, n + 1) == (size_t)(n + 1);
  }
  f.close();
  if (!ok) { SD_MMC.remove(tmp); return false; }
  SD_MMC.remove(PF_FDFRESH);
  return SD_MMC.rename(tmp, PF_FDFRESH);
}
static void pfFreshLoad() {
  fdFreshClearAll(g_pfFresh);
  const String tmp = String(PF_FDFRESH) + ".tmp";
  if (!SD_MMC.exists(PF_FDFRESH) && SD_MMC.exists(tmp)) SD_MMC.rename(tmp, PF_FDFRESH);   // cut between the remove and the rename
  File f = SD_MMC.open(PF_FDFRESH, FILE_READ); if (!f) return;
  while (f.available()) {
    const String l = f.readStringUntil('\n'); FdFreshMark m;
    if (fdFreshParse(l.c_str(), m)) fdFreshAdd(g_pfFresh, m.tag, m.image);   // a line that is not a mark is skipped
  }
  f.close();
}
static bool pfFreshHas(const String &tag, const String &image) { return fdFreshFind(g_pfFresh, tag.c_str(), image.c_str()) >= 0; }
// START FRESH on FluxDrive `tag` for `image`: from now on it gets the original. One gti.log line.
static void pfFreshMark(const String &tag, const String &image) {
  if (pfFreshHas(tag, image)) return;
  if (!fdFreshAdd(g_pfFresh, tag.c_str(), image.c_str())) { gLog("[saves] FluxDrive %s: %s cannot be marked as started fresh (not a tag or path)\n", tag.c_str(), image.c_str()); return; }
  const bool onCard = pfFreshWrite();
  gLog("[saves] FluxDrive %s: %s started fresh - it gets the original until it has an own save again%s\n", tag.c_str(), image.c_str(),
       onCard ? "" : " (NOT written to the card: kept until a restart)");
}
// FluxDrive `tag` has an own save of `image` again (a fetch wrote it, or a fling sent one): the mark goes.
static void pfFreshClear(const String &tag, const String &image) {
  if (!fdFreshDrop(g_pfFresh, tag.c_str(), image.c_str())) return;
  const bool onCard = pfFreshWrite();
  gLog("[saves] FluxDrive %s: %s has an own save again - no longer started fresh%s\n", tag.c_str(), image.c_str(),
       onCard ? "" : " (NOT written to the card)");
}
// ── fd3, the commit window: what the panel keeps in RAM until a FluxDrive's new disk is stored (spec section 6) ──
// Per FluxDrive id: the records flings to it replaced since its disk was last seen stored (fluxdrive_peer.h FdPrevList,
// fdPrevAtFling: up to FD_PREV_MAX, fix round 1), and the START FRESHes whose own save waits as a backup (FdFreshPending,
// one per fresh disk). The sketch's fdCommitWatch forgets them once the disk is stored and puts one back when FluxDrive
// reports its disk again (switched off before the new one was stored). RAM only (the controller's call): a panel restart
// inside the window loses them. Both tables are allocated in PSRAM when first needed (pfWinReady); PF_FDSAVES_MAX lists (one
// per FluxDrive, like the save records) and PF_FDSAVES_MAX pending entries; a full table forgets the entry kept longest.
struct PfPrevSlot { char id[13]; FdPrevList l; };
static PfPrevSlot     *g_pfPrev = nullptr;
static int             g_pfPrevN = 0;
static FdFreshPending *g_pfPend = nullptr;
static int             g_pfPendN = 0;
static bool pfWinReady() {
  if (!g_pfPrev) g_pfPrev = (PfPrevSlot *)ps_calloc(PF_FDSAVES_MAX, sizeof(PfPrevSlot));
  if (!g_pfPend) g_pfPend = (FdFreshPending *)ps_calloc(PF_FDSAVES_MAX, sizeof(FdFreshPending));
  return g_pfPrev && g_pfPend;
}
static void pfPrevDrop(const String &id) {
  for (int i = 0; i < g_pfPrevN; i++) if (id == g_pfPrev[i].id) {
    for (int k = i + 1; k < g_pfPrevN; k++) g_pfPrev[k - 1] = g_pfPrev[k];
    g_pfPrevN--; return; }
}
// That FluxDrive's list, or nullptr. make: an empty one when it has none (nullptr when there is no RAM for the table; a full
// table forgets the FluxDrive kept longest).
static FdPrevList *pfPrevFor(const String &id, bool make = false) {
  for (int i = 0; i < g_pfPrevN; i++) if (id == g_pfPrev[i].id) return &g_pfPrev[i].l;
  if (!make || id.length() >= sizeof g_pfPrev[0].id || !pfWinReady()) return nullptr;
  if (g_pfPrevN >= PF_FDSAVES_MAX) {   // fix round 2: a full table says which FluxDrive's kept records it forgets
    gLog("[saves] FluxDrive %s: its %d kept record(s) are forgotten - records are kept for %d FluxDrives at most\n",
         g_pfPrev[0].l.n ? g_pfPrev[0].l.r[0].tag : g_pfPrev[0].id, g_pfPrev[0].l.n, PF_FDSAVES_MAX);
    for (int k = 1; k < g_pfPrevN; k++) g_pfPrev[k - 1] = g_pfPrev[k]; g_pfPrevN--; }
  PfPrevSlot &s = g_pfPrev[g_pfPrevN++];
  memset(&s, 0, sizeof s); strcpy(s.id, id.c_str()); fdPrevClear(s.l);
  return &s.l;
}
static void pfPendDropAt(int k) {
  if (k < 0 || k >= g_pfPendN) return;
  for (int i = k + 1; i < g_pfPendN; i++) g_pfPend[i - 1] = g_pfPend[i];
  g_pfPendN--;
}
// p becomes a pending START FRESH (fix round 2: one per FluxDrive, fresh disk and backup - fdPendSame, fdPendPut - so a second
// START FRESH, of this game or another, keeps the first one's entry and backup). A full table forgets the oldest entry (*ev,
// *evicted: the caller logs it; its backup stays on the card). false = no RAM.
static bool pfPendPut(const FdFreshPending &p, FdFreshPending *ev, bool *evicted) {
  *evicted = false;
  if (!pfWinReady()) return false;
  const FdFreshPending c = p;   // p may live in this table
  *evicted = fdPendPut(g_pfPend, g_pfPendN, PF_FDSAVES_MAX, c, ev) == FD_PEND_EVICTED;
  return true;
}

// In the sketch, next to the ESP-NOW save fetch: before a FluxDrive's disk goes (another disk, an eject) its unsaved
// writes are fetched (1 fetched, 0 nothing to fetch, -1 failed) - except before START FRESH's fling of the original
// (fresh, fdFetchFirstNeeded); a single-target fling to a FluxDrive sends its own save (fdStageFor / fdUnstage around the
// push) and keeps its record (see there).
static int    fdFetchFirst(const String &ip, bool ui, bool fresh = false, int drive = 0);
static String fdStageFor(const String &ip, int targets);
static void   fdUnstage();
static void   fdRecordFling(const String &ip, uint32_t uid, const String &sent, bool fresh = false, int drive = 0);

// Release the discovery socket + roster. Called from applyRadioMode() on a live MODE switch,
// so the panel does not keep a UDP listener alive in a mode that has no LAN.
static void pfStop() {
  if (g_pfUdpUp) { g_pfUdp.stop(); g_pfUdpUp = false; }
  g_pfPeerN = 0;                 // dongles go; they re-announce within a beacon period
  // g_pfPanelN deliberately kept: pfElect() needs to still see the other screens, or this
  // panel re-claims the leader name while the incumbent is still answering to it.
  g_pfMdnsDirty = true;
}

// Drain one queued fling/command. Call from loop().
static void pfWorker() {
#if defined(GTI_FLEET)   // the web claim/release queue
  if (g_pfEnrollIp.length()) {   // #lock: enroll this panel as an owner of the chosen dongle
    const String ip = g_pfEnrollIp; g_pfEnrollIp = "";
    g_pfBusy = true; String err;
    const bool ok = pfSendEnroll(ip, pfPeerTcp(ip), err);
    if (ok) { for (int i = 0; i < g_pfPeerN; i++) if (g_pfPeers[i].ip == ip) { pfAddMine(g_pfPeers[i].id); g_pfClaimedId = g_pfPeers[i].id; break; } }
    g_pfLastTarget = ip; g_pfLastResult = ok ? "enrolled" : err; g_pfBusy = false;
  }
  if (g_pfUnenrollIp.length()) {   // #lock: release our claim (the dongle drops OUR token only)
    const String ip = g_pfUnenrollIp; g_pfUnenrollIp = "";
    g_pfBusy = true; String err;
    const bool ok = pfSendUnenroll(ip, pfPeerTcp(ip), err);
    if (ok) for (int i = 0; i < g_pfPeerN; i++) if (g_pfPeers[i].ip == ip) { pfDelMine(g_pfPeers[i].id); g_pfReleasedId = g_pfPeers[i].id; break; }
    g_pfLastTarget = ip; g_pfLastResult = ok ? "released" : err; g_pfBusy = false;
  }
#endif
  if (g_pfCmdIp.length()) {
    const String ip = g_pfCmdIp; g_pfCmdIp = ""; const uint8_t cmd = g_pfCmd; g_pfCmd = 0;
    g_pfBusy = true; String err;
    // The web page's commands are ejects (3, 4): a FluxDrive's unsaved writes go to the card first. If that failed the
    // eject still goes, and the result says so (the page has nobody to ask).
    const bool fetched = fdFetchFirst(ip, false) >= 0;
    const bool ok = pfSendCommand(ip, pfPeerTcp(ip), cmd, err);
    g_pfLastTarget = ip; g_pfLastResult = !ok ? err : fetched ? String("ok") : String("saves not fetched - ejected anyway"); g_pfBusy = false;
  }
  if (g_pfSendIp.length()) {
    const String ip = g_pfSendIp; g_pfSendIp = ""; const uint16_t tp = g_pfSendTcp;
    g_pfBusy = true; String err;
    const bool fetched = fdFetchFirst(ip, false) >= 0;   // a FluxDrive's unsaved writes go to the card before the next disk replaces them
    const String sent = fdStageFor(ip, 1);              // one target: a FluxDrive gets its own save, for this push
    const String nm = g_loaded_display.length() ? g_loaded_display : g_loaded_name;
    if (nm.length()) { String nerr; pfSendName(ip, tp, nm, nerr); }   // #24: name-on-fling, best-effort — old dongles ignore the escape
    uint32_t uid = 0;
    const bool ok = pfSendDisk(ip, tp, err, &uid);
    fdUnstage();                                        // the staging as it was
    if (ok) fdRecordFling(ip, uid, sent);               // a FluxDrive's record of the disk it holds now
    g_pfLastTarget = ip; g_pfLastResult = !ok ? err : fetched ? String("ok") : String("saves not fetched - sent anyway"); g_pfBusy = false;
  }
}
