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

#define PF_DISCO_PORT 51703
#define PF_TCP_PORT   3333
#define PF_STALE_MS   40000UL
#define PF_MAX_PEERS  16
#define PF_CMD_EJECT  0x03

struct PfPeer { String id, name, ip, board, fw, disk; uint16_t tcp; bool hd, loaded; uint32_t seen; };
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
  p->hd = pfJf(j, "hd") == "true"; p->loaded = pfJf(j, "loaded") == "true"; p->seen = millis();
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
  for (int guard = 0; guard < 8; guard++) {
    const int sz = g_pfUdp.parsePacket(); if (sz <= 0) break;
    char buf[600]; const int n = g_pfUdp.read((uint8_t *)buf, sizeof(buf) - 1); if (n <= 0) break;
    buf[n] = 0; const String s(buf);
    if (s.indexOf("\"gti\":1") < 0) continue;
    pfUpsert(s);
  }
}

// The roster the web fleet card reads. The panel lists itself first with what
// IT has loaded, so "which device holds which disk" is answered in one view.
static String pfRosterJson() {
  pfPrune();
  const bool busy = g_pfBusy || g_pfSendIp.length() || g_pfCmdIp.length();
  String j = "{\"self\":\"panel\",\"name\":\"" + pfJesc(String("GTi panel")) + "\",";
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
  j += "\"count\":" + String(g_pfPeerN) + ",\"devices\":[";
  for (int i = 0; i < g_pfPeerN; i++) {
    const PfPeer &p = g_pfPeers[i];
    if (i) j += ",";
    j += "{\"id\":\"" + pfJesc(p.id) + "\",\"name\":\"" + pfJesc(p.name) +
         "\",\"ip\":\"" + pfJesc(p.ip) + "\",\"board\":\"" + pfJesc(p.board) +
         "\",\"fw\":\"" + pfJesc(p.fw) + "\",\"hd\":" + (p.hd ? "true" : "false") +
         ",\"loaded\":" + (p.loaded ? "true" : "false") +
         ",\"disk\":\"" + pfJesc(p.disk) + "\",\"tcp\":" + String(p.tcp) +
         ",\"age_s\":" + String((millis() - p.seen) / 1000) + "}";
  }
  j += "]}";
  return j;
}

// Push the loaded disk to a dongle. BLOCKING — loop() only.
static bool pfSendDisk(const String &ip, uint16_t port, String &err) {
  if (!g_loaded || g_img_bytes == 0) { err = "no disk loaded"; return false; }
  const uint8_t *data = g_disk + DATA_LBA * 512;
  const uint32_t size = g_img_bytes;
  WiFiClient c;
  if (!c.connect(ip.c_str(), port, 8000)) { err = "connect failed"; return false; }
  c.setNoDelay(true);   // disable Nagle: with the dongle's delayed-ACK, Nagle stalls each window ~200ms and crawls the fling to ~35 KB/s
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
  if (a != 0x01) { err = (a < 0) ? "no ack" : "dongle refused"; return false; }
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

// Drain one queued fling/command. Call from loop().
static void pfWorker() {
  if (g_pfCmdIp.length()) {
    const String ip = g_pfCmdIp; g_pfCmdIp = ""; const uint8_t cmd = g_pfCmd; g_pfCmd = 0;
    g_pfBusy = true; String err;
    const bool ok = pfSendCommand(ip, PF_TCP_PORT, cmd, err);
    g_pfLastTarget = ip; g_pfLastResult = ok ? "ok" : err; g_pfBusy = false;
  }
  if (g_pfSendIp.length()) {
    const String ip = g_pfSendIp; g_pfSendIp = ""; const uint16_t tp = g_pfSendTcp;
    g_pfBusy = true; String err;
    const bool ok = pfSendDisk(ip, tp, err);
    g_pfLastTarget = ip; g_pfLastResult = ok ? "ok" : err; g_pfBusy = false;
  }
}
