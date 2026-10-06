// sim: the GTi's dongle radio (espnow_server.h API) with simulated Webby dongles instead of a radio.
// Pairing state and CONFIG.TXT keys behave as espnow_server.cpp does; packets from the dongles arrive
// "over the air" from sim_net_tick(), which runs whenever the firmware waits (delay).
#include <Arduino.h>
#include <SD_MMC.h>
#include "espnow_server.h"

volatile bool g_espnow_paired = false, g_espnow_xiao_ready = false, g_espnow_xiao_done = false, g_espnow_xiao_error = false;
volatile bool g_espnow_link_just_established = false;
volatile uint32_t g_espnow_xiao_last_seen = 0;
volatile bool g_dongle_loaded = false;
volatile uint32_t g_dongle_load_id = 0, g_dongle_img_size = 0;
volatile uint8_t g_espnow_dongle_caps = 0, g_espnow_dongle_board = 0;
volatile uint32_t g_espnow_load_id = 0;
volatile bool g_espnow_dirty = false;
volatile uint32_t g_espnow_dirty_loadid = 0;
volatile uint16_t g_espnow_dirty_count = 0;
volatile uint32_t g_espnow_dirty_size = 0;

static char _apName[24], _apPass[16];
static void apNames() { if (!_apName[0]) { snprintf(_apName, sizeof _apName, GTI_AP_PREFIX "%02X%02X", 0x58, 0xB0); snprintf(_apPass, sizeof _apPass, GTI_AP_PASS_PRE "%02X%02X", 0x58, 0xB0); } }
const char *espnowApName() { apNames(); return _apName; }
const char *espnowApPass() { apNames(); return _apPass; }

// ── the simulated dongles ──
#define SIM_DONGLES 3
struct SimDongle {
  uint8_t mac[6]; bool powered; bool hd;          // hd = XIAO-class (2 MB RAM disk)
  bool loaded; uint32_t load_id; uint32_t size; char name[64];
  uint8_t *img; uint8_t dirty[256]; uint16_t ndirty;
  uint32_t next_status;
};
static SimDongle D[SIM_DONGLES] = {
  {{0x34, 0x85, 0x18, 0x9A, 0x80, 0xFE}, true, false},
  {{0xD8, 0x3B, 0xDA, 0x41, 0x2C, 0x10}, true, true},
  {{0x34, 0x85, 0x18, 0x7B, 0x11, 0x42}, false, false},
};
static bool s_started = false, s_scan = false;
static int s_scanned[SIM_DONGLES]; static int s_nscanned = 0; static int s_cap = 32;
static uint8_t _xiao_mac[6]; static String _xiao_ip;
static String s_fling = "DISK.ADF";
static uint32_t s_hello_due = 0; static bool s_hello_pending = false;
static ClaimAskCb s_claimAsk = nullptr; static String s_myName; static bool s_claimCancelled = false;

static int findMac(const uint8_t *m) { for (int i = 0; i < SIM_DONGLES; i++) if (!memcmp(D[i].mac, m, 6)) return i; return -1; }
static int active() { return g_espnow_paired ? findMac(_xiao_mac) : -1; }
static void notify(int what) { js_event(30, what, active(), ""); }
static String macStr(const uint8_t *m) { char b[18]; snprintf(b, sizeof b, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]); return String(b); }

static void writeConfigPair() {
  String lines = ""; bool w1 = false, w2 = false;
  File fr = SD_MMC.open("/CONFIG.TXT", FILE_READ);
  if (fr) { while (fr.available()) { String line = fr.readStringUntil('\n'); line.trim();
      if (line.startsWith("XIAO_MAC=")) { lines += "XIAO_MAC=" + macStr(_xiao_mac) + "\n"; w1 = true; }
      else if (line.startsWith("XIAO_IP=")) { lines += "XIAO_IP=" + _xiao_ip + "\n"; w2 = true; }
      else lines += line + "\n"; } fr.close(); }
  if (!w1) lines += "XIAO_MAC=" + macStr(_xiao_mac) + "\n";
  if (!w2) lines += "XIAO_IP=" + _xiao_ip + "\n";
  File fw = SD_MMC.open("/CONFIG.TXT", FILE_WRITE); if (fw) { fw.print(lines); fw.close(); }
}
static void pairWith(int i) {
  memcpy(_xiao_mac, D[i].mac, 6); _xiao_ip = DONGLE_AP_IP;
  g_espnow_paired = true; g_espnow_link_just_established = true; g_espnow_xiao_last_seen = millis();
  g_espnow_dongle_caps = 3; g_espnow_dongle_board = D[i].hd ? 1 : 0;
  writeConfigPair();
  Serial.printf("[NOW] Paired: MAC=%s IP=%s\n", macStr(_xiao_mac).c_str(), _xiao_ip.c_str());
  notify(1);
}

// called from delay(): deliver what the dongles "send"
extern "C" void sim_net_tick(void) {
  if (!s_started) return;
  uint32_t now = (uint32_t)millis();
  if (s_hello_pending && (int32_t)(now - s_hello_due) >= 0) {
    s_hello_pending = false;
    for (int i = 0; i < SIM_DONGLES; i++) {
      if (!D[i].powered) continue;
      if (s_scan) {
        bool have = false; for (int k = 0; k < s_nscanned; k++) if (s_scanned[k] == i) have = true;
        if (!have && s_nscanned < s_cap) s_scanned[s_nscanned++] = i;
        g_espnow_xiao_last_seen = now;
      } else if (!g_espnow_paired) { pairWith(i); break; }
    }
  }
  int a = active();
  if (a >= 0 && D[a].powered && (int32_t)(now - D[a].next_status) >= 0) {   // load-state heartbeat (+ dirty beacon)
    D[a].next_status = now + 2000;
    g_espnow_xiao_last_seen = now;
    g_dongle_loaded = D[a].loaded; g_dongle_load_id = D[a].load_id; g_dongle_img_size = D[a].size;
    if (D[a].ndirty) { g_espnow_dirty_loadid = D[a].load_id; g_espnow_dirty_count = D[a].ndirty; g_espnow_dirty_size = D[a].size; g_espnow_dirty = true; }
  }
}

void espnowBegin() {
  s_started = true;
  File f = SD_MMC.open("/CONFIG.TXT", FILE_READ);
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n'); line.trim();
      if (line.startsWith("#")) continue;
      if (line.startsWith("XIAO_MAC=")) {
        unsigned m[6] = {0};
        if (sscanf(line.substring(9).c_str(), "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
          bool z = true; for (int i = 0; i < 6; i++) { _xiao_mac[i] = (uint8_t)m[i]; if (m[i]) z = false; }
          if (!z) g_espnow_paired = true;
        }
      }
      if (line.startsWith("XIAO_IP=")) _xiao_ip = line.substring(8);
    }
    f.close();
  }
  int a = active(); if (a >= 0) { g_espnow_dongle_caps = 3; g_espnow_dongle_board = D[a].hd ? 1 : 0; }
  Serial.println("[NOW] begin OK (simulated radio)");
  notify(0);
}
void espnowStop() { s_started = false; notify(0); }
void espnowBroadcastHello() { if (!s_started || s_hello_pending) return; s_hello_pending = true; s_hello_due = (uint32_t)millis() + 120; }
bool espnowXiaoOnline() { if (!g_espnow_paired || !g_espnow_xiao_last_seen) return false; return (millis() - g_espnow_xiao_last_seen) < 30000; }
bool espnowIsPaired() { return g_espnow_paired; }
String espnowGetXiaoMac() { return macStr(_xiao_mac); }
String espnowGetSSIDLabel() { return "WiFi+NOW"; }

void espnowScanBegin() { s_nscanned = 0; s_scan = true; }
void espnowScanEnd() { s_scan = false; }
int espnowScanCount() { return s_nscanned; }
String espnowScanGetMac(int i) { return (i < 0 || i >= s_nscanned) ? String("") : macStr(D[s_scanned[i]].mac); }
bool espnowScanSelect(int i) { if (i < 0 || i >= s_nscanned) return false; s_scan = false; pairWith(s_scanned[i]); return true; }
void espnowSetScanCap(int n) { s_cap = n < 1 ? 1 : n; }
void espnowScanMacBytes(int i, uint8_t *out) { if (i >= 0 && i < s_nscanned) memcpy(out, D[s_scanned[i]].mac, 6); }
void espnowSendUnpair(const uint8_t *mac) { delay(90); (void)mac; }
void espnowSendLock(const uint8_t *mac) { delay(90); (void)mac; }
void espnowSendUnlock(const uint8_t *mac) { delay(90); (void)mac; }
void espnowSendShare(const uint8_t *mac) { delay(90); (void)mac; }
void espnowForgetActive(const uint8_t *mac) {
  if (memcmp(_xiao_mac, mac, 6)) return;
  g_espnow_paired = false; memset(_xiao_mac, 0, 6); _xiao_ip = "";
  String lines = ""; File fr = SD_MMC.open("/CONFIG.TXT", FILE_READ);
  if (fr) { while (fr.available()) { String line = fr.readStringUntil('\n'); line.trim();
      if (line.startsWith("XIAO_MAC=") || line.startsWith("XIAO_IP=")) continue; lines += line + "\n"; } fr.close(); }
  File fw = SD_MMC.open("/CONFIG.TXT", FILE_WRITE); if (fw) { fw.print(lines); fw.close(); }
  notify(1);
}
String espnowScanInUseBy(int i) { (void)i; return ""; }
void espnowSetClaimAsk(ClaimAskCb cb) { s_claimAsk = cb; }
void espnowSetScreenName(const String &n) { s_myName = n; }
bool espnowClaimCancelled() { return s_claimCancelled; }
bool espnowSendNotify(const String &, const String &, uint32_t) { g_espnow_xiao_ready = g_espnow_xiao_done = g_espnow_xiao_error = false; return true; }
void espnowSetFlingName(const String &n) { s_fling = n.length() ? n : String("DISK.ADF"); }

static bool sendCore(int i, uint32_t size) {
  Serial.printf("[TCP] Connecting to XIAO at %s:%d\n", DONGLE_AP_IP, DONGLE_TCP_PORT);
  if (i < 0 || !D[i].powered) {           // the dongle's Wi-Fi never shows up
    delay(1600); Serial.println("\n[TCP] WiFi connect failed — restarting ESP-NOW");
    g_espnow_xiao_error = true; return false;
  }
  s_claimCancelled = false;
  delay(700);                              // join the dongle's Wi-Fi
  Serial.println("[TCP] WiFi connected. IP: 192.168.4.2");
  uint8_t *src = g_disk + ESPNOW_DATA_LBA * ESPNOW_SECTOR_SIZE;
  uint8_t *img = (uint8_t *)realloc(D[i].img, size ? size : 1);
  if (!img) { g_espnow_xiao_error = true; return false; }
  D[i].img = img;
  for (uint32_t sent = 0; sent < size; sent += 65536) {   // ~650 KB/s, like the real link
    uint32_t n = size - sent < 65536 ? size - sent : 65536;
    memcpy(img + sent, src + sent, n); delay(100);
  }
  D[i].size = size; D[i].loaded = true; D[i].load_id = esp_random() | 1; D[i].ndirty = 0; memset(D[i].dirty, 0, sizeof D[i].dirty);
  strncpy(D[i].name, s_fling.c_str(), sizeof D[i].name - 1);
  g_espnow_load_id = D[i].load_id;
  Serial.printf("[TCP] Sent %lu bytes\n[TCP] XIAO response: 0x01 (OK)\n[TCP] load_id=%lu\n", (unsigned long)size, (unsigned long)g_espnow_load_id);
  delay(200);
  g_espnow_xiao_done = true; g_espnow_xiao_last_seen = millis();
  D[i].next_status = 0;
  js_event(31, i, (int)size, D[i].name);
  return true;
}
bool espnowSendDisk(uint32_t size) { return sendCore(active(), size); }
bool espnowSendDiskTo(const uint8_t *mac, uint32_t size) { return sendCore(findMac(mac), size); }
bool espnowSendDiskHome(const String &ssid, const String &, String &, uint32_t) {
  Serial.printf("[HOME] joining %s ... not in range (simulator)\n", ssid.c_str()); delay(1500);
  g_espnow_xiao_error = true; return false;
}
void espnowSendEject() {
  int a = active(); if (a < 0 || !D[a].powered) return;
  D[a].loaded = false; D[a].load_id = 0; D[a].ndirty = 0; D[a].next_status = 0;
  js_event(32, a, 0, "");
}
bool espnowFetchSave(SavePersistCb persist) {
  int a = active(); if (a < 0 || !persist) return false;
  Serial.printf("[SAVE] Fetching dirty sectors from %s\n", DONGLE_AP_IP);
  if (!D[a].powered) { delay(1600); Serial.println("[SAVE] WiFi join failed"); return false; }
  delay(600);
  uint16_t mapLen = (uint16_t)((D[a].size / 512 + 7) / 8); if (mapLen > 256) mapLen = 256;
  uint32_t n = 0; for (uint32_t s = 0; s < (uint32_t)mapLen * 8; s++) if ((D[a].dirty[s >> 3] >> (s & 7)) & 1) n++;
  uint8_t *packed = (uint8_t *)malloc(n ? n * 512 : 1); uint32_t k = 0;
  for (uint32_t s = 0; s < (uint32_t)mapLen * 8; s++) if ((D[a].dirty[s >> 3] >> (s & 7)) & 1) memcpy(packed + 512 * k++, D[a].img + s * 512, 512);
  Serial.printf("[SAVE] load=%lu img=%lu dirty=%lu\n", (unsigned long)D[a].load_id, (unsigned long)D[a].size, (unsigned long)n);
  bool ok = persist(D[a].load_id, D[a].size, D[a].dirty, mapLen, packed, n);
  free(packed);
  Serial.printf("[SAVE] %s\n", ok ? "persisted + acked" : "persist FAILED — no ack");
  if (ok) { D[a].ndirty = 0; memset(D[a].dirty, 0, sizeof D[a].dirty); g_espnow_dirty = false; js_event(33, a, 0, ""); }
  return ok;
}

// ── page controls ──
extern "C" {
__attribute__((export_name("sim_dongle_power"))) void sim_dongle_power(int i, int on) { if (i >= 0 && i < SIM_DONGLES) { D[i].powered = on; if (!on) { D[i].loaded = false; D[i].ndirty = 0; } js_event(34, i, on, ""); } }
// the Amiga writes a save: a few sectors in the second half of the image change
__attribute__((export_name("sim_dongle_game_save"))) int sim_dongle_game_save(int i) {
  if (i < 0 || i >= SIM_DONGLES || !D[i].loaded || !D[i].img || D[i].size < 4096) return 0;
  uint32_t nsec = D[i].size / 512; if (nsec > 2048) nsec = 2048;
  uint32_t base = nsec * 3 / 4;
  for (uint32_t s = base; s < base + 3 && s < nsec; s++) {
    uint8_t *p = D[i].img + s * 512; const char msg[] = "GTi simulator: a saved game lives here. ";
    for (int b = 0; b < 512; b++) p[b] = (uint8_t)msg[b % (sizeof msg - 1)] ^ (uint8_t)(esp_random() & 0x1F);
    if (!((D[i].dirty[s >> 3] >> (s & 7)) & 1)) { D[i].dirty[s >> 3] |= (uint8_t)(1 << (s & 7)); D[i].ndirty++; }
  }
  D[i].next_status = 0; js_event(35, i, D[i].ndirty, "");
  return D[i].ndirty;
}
static char s_json[1024];
__attribute__((export_name("sim_dongle_json"))) const char *sim_dongle_json(void) {
  int a = active(); int o = snprintf(s_json, sizeof s_json, "{\"radio\":%d,\"paired\":%d,\"active\":%d,\"d\":[", s_started ? 1 : 0, g_espnow_paired ? 1 : 0, a);
  for (int i = 0; i < SIM_DONGLES; i++)
    o += snprintf(s_json + o, sizeof s_json - o, "%s{\"mac\":\"%s\",\"on\":%d,\"hd\":%d,\"loaded\":%d,\"size\":%lu,\"name\":\"%s\",\"dirty\":%u}",
                  i ? "," : "", macStr(D[i].mac).c_str(), D[i].powered, D[i].hd, D[i].loaded, (unsigned long)D[i].size, D[i].loaded ? D[i].name : "", D[i].ndirty);
  snprintf(s_json + o, sizeof s_json - o, "]}");
  return s_json;
}
__attribute__((export_name("sim_dongle_image"))) const uint8_t *sim_dongle_image(int i) { return (i >= 0 && i < SIM_DONGLES && D[i].loaded) ? D[i].img : nullptr; }
}
