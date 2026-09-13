// panel_fleet.h — the JC3248 as the fleet controller: hear Webby dongle
// discovery beacons on the LAN, keep a roster, and fling the loaded disk to a
// chosen dongle over TCP. Ported from the OMEGAWARE fleet.h and adapted to
// this firmware's structures (g_disk / g_sv_img_size / g_loaded_name).
//
// Wire protocol is Webby's, unchanged (#20): UDP 51703 JSON "gti":1 beacons
// (40 s stale), TCP 3333 = 4-byte BE size + raw bytes, ack 0x01 + 4-byte LE
// load id; 0xFFFFFFFF escape → 1-byte command (0x03 eject).
//
// Non-blocking rule (Mez's, non-negotiable): the HTTP handler only QUEUES a
// fling/command; loop() executes it. A transfer inside a handler serves
// nothing for its whole duration, including the endpoint you would debug with.
#pragma once
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>   // #clubday: the fleet controller re-registers its own mDNS name after the panel-vs-panel election

#define PF_DISCO_PORT 51703
#define PF_TCP_PORT   3333
#define PF_STALE_MS   40000UL
#define PF_MAX_PEERS  16
#define PF_CMD_EJECT   0x03
#define PF_CMD_SETNAME 0x06   // #24: tell the dongle the pretty display name for the NEXT disk (its FAT12 root stays OMEGA.ADF)
#define PF_CMD_ENROLL  0x07   // #lock: enroll this panel's token as an owner (dongle's enroll window must be open)
#define PF_CMD_AUTH    0x08   // #lock: token preamble before a disk fling, proving this panel is an enrolled owner
#define PF_CMD_UNENROLL 0x09  // #lock: remove this panel's token from the dongle's owner list (unclaim/release)
#define PF_TOKEN_LEN   16     // #lock: owner-token length (bytes)

struct PfPeer { String id, name, ip, board, fw, disk; uint16_t tcp; bool hd, loaded, lk, lkd, enr; uint32_t seen; };   // #lock: lk=lock-capable, lkd=currently locked, enr=enroll window open
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
static String   g_pfTargetIp, g_pfTargetName;
static uint16_t g_pfTargetTcp = PF_TCP_PORT;
// #lock: this panel's owner token (identity). Generated once, persisted as PANEL_TOKEN in CONFIG.TXT
// by the sketch (loadConfig, which has SD helpers). pfSendDisk prepends it as AUTH to lock-capable dongles.
static uint8_t  g_panel_token[PF_TOKEN_LEN] = {0};
static bool     g_panel_token_ok = false;
static String   g_pfEnrollIp;   // queued by the web handler; pfWorker sends CMD_ENROLL to this dongle
static String   g_pfEnrollResult;
// #lock: ids (MACs) of dongles THIS screen owns (claimed). Loaded at boot from CONFIG.TXT
// (DONGLE_<mac>.MINE=1) by the sketch; used to hide locked-not-mine dongles and to gate UNCLAIM.
static String   g_mineIds[16];
static int      g_mineN = 0;
static bool pfIsMine(const String &id){ for(int i=0;i<g_mineN;i++) if(g_mineIds[i]==id) return true; return false; }
static void pfAddMine(const String &id){ if(!id.length()||pfIsMine(id)) return; if(g_mineN<16) g_mineIds[g_mineN++]=id; }
static void pfDelMine(const String &id){ for(int i=0;i<g_mineN;i++) if(g_mineIds[i]==id){ for(int j=i;j<g_mineN-1;j++) g_mineIds[j]=g_mineIds[j+1]; g_mineN--; return; } }
// #console: a locked dongle is only visible on a screen that owns it — EXCEPT while it is in
// pairing mode (enr), so any screen can still claim a dongle whose BOOT was just tapped.
static bool pfPeerVisible(const PfPeer &p){ return p.enr || pfIsMine(p.id) || !p.lkd; }

// ── Panel-vs-panel election (#clubday: many screens on one Wi-Fi, one gotekomega.local) ──
// Every screen beacons role:panel with its MAC id and the mDNS name it currently holds.
// Among screens that still want the default "gotekomega", the LOWEST MAC keeps
// gotekomega.local; the rest fall back to gotekomega-<mac>.local (same scheme the dongles
// use). A screen given a custom MDNS_NAME never contends. Deterministic, no mDNS probing.
struct PfPanel { String id, mdns; uint32_t seen; };
static PfPanel  g_pfPanels[PF_MAX_PEERS];
static int      g_pfPanelN = 0;
static String   g_pfMyId;              // this screen's MAC (uppercase hex) — same format as the beacon id
static String   g_pfMdnsName;          // the mDNS name we are actually registered under (no ".local")
static bool     g_pfIsLeader = true;   // do we own the base gotekomega.local?
static bool     g_pfMdnsDirty = true;  // set when the elected name changes -> re-register (no reboot)

static String pfMyMac() {
  uint8_t m[6]; WiFi.macAddress(m);
  char b[13]; snprintf(b, sizeof(b), "%02X%02X%02X%02X%02X%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(b);
}
static String pfMacSuffix() {   // last 2 bytes, lowercase — matches the dongle's gotekomega-<mac> scheme
  uint8_t m[6]; WiFi.macAddress(m);
  char b[8]; snprintf(b, sizeof(b), "%02x%02x", m[4], m[5]);
  return String(b);
}
static String pfMdnsName() { return g_pfMdnsName.length() ? g_pfMdnsName : g_mdns_name; }

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
static void pfElect() {
  if (!g_pfMyId.length()) g_pfMyId = pfMyMac();
  pfPanelPrune();
  String want;
  if (g_mdns_name != "gotekomega") {           // a named screen keeps its own name, never contends
    want = g_mdns_name; g_pfIsLeader = true;
  } else {
    bool yield = false;                         // yield gotekomega to any live screen with a lower MAC that also wants it
    for (int i = 0; i < g_pfPanelN; i++)
      if (g_pfPanels[i].mdns == "gotekomega" && g_pfPanels[i].id < g_pfMyId) { yield = true; break; }
    want = yield ? (String("gotekomega-") + pfMacSuffix()) : String("gotekomega");
    g_pfIsLeader = !yield;
  }
  if (want != g_pfMdnsName) { g_pfMdnsName = want; g_pfMdnsDirty = true; }
}
// Re-register mDNS when the elected name changed. Safe to call every pass — only acts when dirty.
static void pfApplyMdns() {
  if (!g_pfMdnsDirty || g_pfMdnsName.length() == 0) return;
  MDNS.end();
  if (MDNS.begin(g_pfMdnsName.c_str())) MDNS.addService("http", "tcp", 80);
  g_pfMdnsDirty = false;
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
    if (now - g_pfPeers[i].seen < PF_STALE_MS) { if (w != i) g_pfPeers[w] = g_pfPeers[i]; w++; }
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
  p->lk = pfJf(j, "lk") == "1"; p->lkd = pfJf(j, "lkd") == "1";   // #lock: lock-capable + currently-locked
  p->enr = pfJf(j, "enr") == "1";                                 // #lock: enroll window open -> the screen offers to CLAIM
  p->seen = millis();
}

// #rule: announce this panel as the fleet LEADER on the shared discovery port, so
// the dongles cede gotekomega.local to it and stay gotekomega-<mac>.local. A screen,
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
    pfUpsert(s);
  }
  pfElect();        // #clubday: pick our mDNS name from the screens we hear (lowest MAC keeps gotekomega.local)
  pfApplyMdns();    // re-register if it changed — no reboot
  pfSendBeacon();   // #rule: keep announcing ourselves (with the elected name) as a leader
}

// The roster the web fleet card reads. The panel lists itself first with what
// IT has loaded, so "which device holds which disk" is answered in one view.
static String pfRosterJson() {
  pfPrune();
  const bool busy = g_pfBusy || g_pfSendIp.length() || g_pfCmdIp.length();
  String j = "{\"self\":\"panel\",\"name\":\"" + pfJesc(String("GTi panel")) + "\",";
  j += "\"mdns\":\"" + pfJesc(pfMdnsName()) + "\",";                       // #clubday: the mDNS name this screen actually holds after the election
  j += "\"leader\":" + String(g_pfIsLeader ? "true" : "false") + ",";     // #clubday: true = owns gotekomega.local
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
         ",\"lk\":" + (p.lk ? "true" : "false") + ",\"lkd\":" + (p.lkd ? "true" : "false") +
         ",\"mine\":" + (pfIsMine(p.id) ? "true" : "false") +
         ",\"age_s\":" + String((millis() - p.seen) / 1000) + "}";
  }
  j += "]}";
  return j;
}

// #lock: is the dongle at this IP running lock-capable firmware (advertised "lk":1)?
static bool pfPeerLockCapable(const String &ip) {
  for (int i = 0; i < g_pfPeerN; i++) if (g_pfPeers[i].ip == ip) return g_pfPeers[i].lk;
  return false;
}

// Push the loaded disk to a dongle. BLOCKING — loop() only.
static bool pfSendDisk(const String &ip, uint16_t port, String &err) {
  if (!g_loaded || g_img_bytes == 0) { err = "no disk loaded"; return false; }
  const uint8_t *data = g_disk + DATA_LBA * 512;
  const uint32_t size = g_img_bytes;
  WiFiClient c;
  if (!c.connect(ip.c_str(), port, 8000)) { err = "connect failed"; return false; }
  c.setNoDelay(true);   // disable Nagle: with the dongle's delayed-ACK, Nagle stalls each window ~200ms and crawls the fling to ~35 KB/s
  if (g_panel_token_ok && pfPeerLockCapable(ip)) {   // #lock: AUTH preamble — the dongle validates the token, then reads the size+disk below
    uint8_t a[5 + PF_TOKEN_LEN]; a[0]=a[1]=a[2]=a[3]=0xFF; a[4]=PF_CMD_AUTH; memcpy(a+5, g_panel_token, PF_TOKEN_LEN);
    c.write(a, sizeof(a));
  }
  const uint8_t hdr[4] = { (uint8_t)(size >> 24), (uint8_t)(size >> 16), (uint8_t)(size >> 8), (uint8_t)size };
  const uint32_t tSend0 = millis();   // time the bulk transfer for the throughput report
  c.write(hdr, 4);
  uint32_t sent = 0, stall = millis();
  while (sent < size) {
    if (!c.connected()) { err = "connection lost at " + String(sent) + "B"; c.stop(); return false; }
    uint32_t chunk = size - sent; if (chunk > 8192) chunk = 8192;
    const size_t w = c.write(data + sent, chunk);
    if (w == 0) { if (millis() - stall > 15000) { err = "send stalled"; c.stop(); return false; } delay(2); continue; }
    sent += w; stall = millis();
  }
  g_pfLastMs = millis() - tSend0; g_pfLastBytes = size;
  g_pfLastKbps = g_pfLastMs ? (uint32_t)((uint64_t)size * 1000ULL / g_pfLastMs / 1024ULL) : 0;
  const uint32_t t0 = millis();
  while (c.available() < 1 && millis() - t0 < 10000) { if (!c.connected()) break; delay(5); }
  const int a = c.available() >= 1 ? c.read() : -1;
  c.stop();
  if (a != 0x01) { err = (a==0x05) ? "locked to another screen" : (a < 0) ? "no ack" : "dongle refused"; return false; }   // #lock: 0x05 = the dongle rejected us (not an owner)
  return true;
}

static bool pfSendCommand(const String &ip, uint16_t port, uint8_t cmd, String &err) {
  WiFiClient c;
  if (!c.connect(ip.c_str(), port, 5000)) { err = "connect failed"; return false; }
  const uint8_t f[5] = { 0xFF, 0xFF, 0xFF, 0xFF, cmd };
  c.write(f, 5);
  const uint32_t t0 = millis();
  while (c.available() < 1 && millis() - t0 < 3000) { if (!c.connected()) break; delay(5); }
  const int a = c.available() >= 1 ? c.read() : -1;
  c.stop();
  if (a < 0) { err = "no reply"; return false; }
  return true;
}

// #24: set-next-name — tell the dongle the pretty display name before the disk.
// Best-effort: an old dongle ignores the unknown escape (one-sided safe), so a
// failure here never blocks the fling.
static bool pfSendName(const String &ip, uint16_t port, const String &name, String &err) {
  WiFiClient c;
  if (!c.connect(ip.c_str(), port, 4000)) { err = "connect failed"; return false; }
  c.setNoDelay(true);
  String nm = name; if (nm.length() > 120) nm = nm.substring(0, 120);
  const uint8_t hdr[6] = { 0xFF, 0xFF, 0xFF, 0xFF, PF_CMD_SETNAME, (uint8_t)nm.length() };
  c.write(hdr, 6);
  if (nm.length()) c.write((const uint8_t *)nm.c_str(), nm.length());
  const uint32_t t0 = millis();
  while (c.available() < 1 && millis() - t0 < 3000) { if (!c.connected()) break; delay(3); }
  const int a = c.available() >= 1 ? c.read() : -1;
  c.stop();
  if (a != 0x01) { err = (a < 0) ? "no ack" : "not supported"; return false; }
  return true;
}

// #lock: enroll this panel's token as an owner of the dongle at ip. The dongle's enroll
// window must be open (short BOOT tap on the dongle). Short best-effort exchange.
static bool pfSendEnroll(const String &ip, uint16_t port, String &err) {
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

// #lock: release ownership — tell the dongle to drop THIS panel's token. Blocking, short.
static bool pfSendUnenroll(const String &ip, uint16_t port, String &err) {
  WiFiClient c;
  if (!c.connect(ip.c_str(), port, 4000)) { err = "connect failed"; return false; }
  c.setNoDelay(true);
  uint8_t a[5 + PF_TOKEN_LEN]; a[0]=a[1]=a[2]=a[3]=0xFF; a[4]=PF_CMD_UNENROLL; memcpy(a+5, g_panel_token, PF_TOKEN_LEN);
  c.write(a, sizeof(a));
  const uint32_t t0 = millis();
  while (c.available() < 1 && millis()-t0 < 4000) { if(!c.connected()) break; delay(3); }
  const int r = c.available() >= 1 ? c.read() : -1;
  c.stop();
  if (r == 0x01) return true;
  err = (r<0) ? "no reply" : "refused"; return false;
}

// Drain one queued fling/command. Call from loop().
static void pfWorker() {
  if (g_pfEnrollIp.length()) {   // #lock: enroll this panel as an owner of the chosen dongle
    const String ip = g_pfEnrollIp; g_pfEnrollIp = "";
    g_pfBusy = true; String err;
    const bool ok = pfSendEnroll(ip, PF_TCP_PORT, err);
    g_pfEnrollResult = ok ? "enrolled" : err; g_pfLastTarget = ip; g_pfLastResult = ok ? "enrolled" : err; g_pfBusy = false;
  }
  if (g_pfCmdIp.length()) {
    const String ip = g_pfCmdIp; g_pfCmdIp = ""; const uint8_t cmd = g_pfCmd; g_pfCmd = 0;
    g_pfBusy = true; String err;
    const bool ok = pfSendCommand(ip, PF_TCP_PORT, cmd, err);
    g_pfLastTarget = ip; g_pfLastResult = ok ? "ok" : err; g_pfBusy = false;
  }
  if (g_pfSendIp.length()) {
    const String ip = g_pfSendIp; g_pfSendIp = ""; const uint16_t tp = g_pfSendTcp;
    g_pfBusy = true; String err;
    const String nm = g_loaded_display.length() ? g_loaded_display : g_loaded_name;
    if (nm.length()) { String nerr; pfSendName(ip, tp, nm, nerr); }   // #24: name-on-fling, best-effort — old dongles ignore the escape
    const bool ok = pfSendDisk(ip, tp, err);
    g_pfLastTarget = ip; g_pfLastResult = ok ? "ok" : err; g_pfBusy = false;
  }
}
