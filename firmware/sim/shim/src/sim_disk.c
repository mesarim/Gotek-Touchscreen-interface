// sim: IDF-style ff_diskio dispatch + the "SD card" driver (sectors come from the web page).
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "ff.h"
#include "diskio.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sim_host.h"

static const ff_diskio_impl_t *s_impls[FF_VOLUMES];
static sdmmc_card_t *s_cards[FF_VOLUMES];
PARTITION VolToPart[FF_VOLUMES] = {{0, 0}, {1, 0}};

void ff_diskio_register(BYTE pdrv, const ff_diskio_impl_t *impl) {
  if (pdrv >= FF_VOLUMES) return;
  s_impls[pdrv] = impl;
  if (!impl) s_cards[pdrv] = NULL;
}
esp_err_t ff_diskio_get_drive(BYTE *out_pdrv) {
  for (BYTE i = 0; i < FF_VOLUMES; i++) if (!s_impls[i]) { *out_pdrv = i; return ESP_OK; }
  return ESP_ERR_NOT_FOUND;
}
DSTATUS disk_initialize(BYTE pdrv) { return (pdrv < FF_VOLUMES && s_impls[pdrv]) ? s_impls[pdrv]->init(pdrv) : STA_NOINIT; }
DSTATUS disk_status(BYTE pdrv) { return (pdrv < FF_VOLUMES && s_impls[pdrv]) ? s_impls[pdrv]->status(pdrv) : STA_NOINIT; }
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) { return (pdrv < FF_VOLUMES && s_impls[pdrv]) ? s_impls[pdrv]->read(pdrv, buff, sector, count) : RES_NOTRDY; }
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) { return (pdrv < FF_VOLUMES && s_impls[pdrv]) ? s_impls[pdrv]->write(pdrv, buff, sector, count) : RES_NOTRDY; }
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) { return (pdrv < FF_VOLUMES && s_impls[pdrv]) ? s_impls[pdrv]->ioctl(pdrv, cmd, buff) : RES_NOTRDY; }

// ── the card ──
esp_err_t sdmmc_read_sectors(sdmmc_card_t *card, void *dst, size_t start, size_t n) {
  (void)card; return js_disk_read((uint8_t *)dst, (uint32_t)start, (uint32_t)n) ? ESP_OK : ESP_FAIL;
}
esp_err_t sdmmc_write_sectors(sdmmc_card_t *card, const void *src, size_t start, size_t n) {
  (void)card; return js_disk_write((const uint8_t *)src, (uint32_t)start, (uint32_t)n) ? ESP_OK : ESP_FAIL;
}
esp_err_t sdmmc_get_status(sdmmc_card_t *card) { (void)card; return js_disk_present() ? ESP_OK : ESP_FAIL; }
esp_err_t sdmmc_host_init(void) { return ESP_OK; }
esp_err_t sdmmc_host_init_slot(int slot, const sdmmc_slot_config_t *cfg) { (void)slot; (void)cfg; return ESP_OK; }
esp_err_t sdmmc_host_deinit(void) { return ESP_OK; }
esp_err_t sdmmc_card_init(const sdmmc_host_t *host, sdmmc_card_t *c) { (void)host; memset(c, 0, sizeof *c); return js_disk_present() ? ESP_OK : ESP_FAIL; }
esp_err_t sdmmc_host_set_card_clk(int slot, uint32_t f) { (void)slot; (void)f; return ESP_OK; }
esp_err_t sdmmc_host_get_real_freq(int slot, int *f) { (void)slot; if (f) *f = 40000; return ESP_OK; }

static DSTATUS sd_init(BYTE p) { (void)p; return js_disk_present() ? 0 : STA_NODISK; }
static DSTATUS sd_status(BYTE p) { (void)p; return js_disk_present() ? 0 : STA_NODISK; }
static DRESULT sd_read(BYTE p, BYTE *b, LBA_t s, UINT c) { return sdmmc_read_sectors(s_cards[p], b, s, c) == ESP_OK ? RES_OK : RES_ERROR; }
static DRESULT sd_write(BYTE p, const BYTE *b, LBA_t s, UINT c) { return sdmmc_write_sectors(s_cards[p], b, s, c) == ESP_OK ? RES_OK : RES_ERROR; }
static DRESULT sd_ioctl(BYTE p, BYTE cmd, void *b) {
  (void)p;
  switch (cmd) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_COUNT: *(LBA_t *)b = js_disk_sectors(); return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)b = 512; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)b = 1; return RES_OK;
    case CTRL_TRIM: return RES_OK;
  }
  return RES_ERROR;
}
static const ff_diskio_impl_t s_sd_impl = {sd_init, sd_status, sd_read, sd_write, sd_ioctl};
void ff_diskio_register_sdmmc(unsigned char pdrv, sdmmc_card_t *card) {
  if (pdrv >= FF_VOLUMES) return;
  s_cards[pdrv] = card; s_impls[pdrv] = card ? &s_sd_impl : NULL;
}
BYTE ff_diskio_get_pdrv_card(const sdmmc_card_t *card) {
  for (BYTE i = 0; i < FF_VOLUMES; i++) if (s_cards[i] == card && s_impls[i] == &s_sd_impl) return i;
  return 0xFF;
}

// ── FatFs OS layer ──
void *ff_memalloc(UINT n) { return malloc(n); }
void ff_memfree(void *p) { free(p); }
int ff_mutex_create(int vol) { (void)vol; return 1; }
void ff_mutex_delete(int vol) { (void)vol; }
int ff_mutex_take(int vol) { (void)vol; return 1; }
void ff_mutex_give(int vol) { (void)vol; }
DWORD get_fattime(void) {
  double ms = js_epoch_ms(); time_t t = (time_t)(ms / 1000.0); struct tm tmv; localtime_r(&t, &tmv);
  return ((DWORD)(tmv.tm_year - 80) << 25) | ((DWORD)(tmv.tm_mon + 1) << 21) | ((DWORD)tmv.tm_mday << 16) |
         ((DWORD)tmv.tm_hour << 11) | ((DWORD)tmv.tm_min << 5) | ((DWORD)tmv.tm_sec >> 1);
}

#include <sys/time.h>
int settimeofday(const struct timeval *tv, const void *tz) { (void)tv; (void)tz; return 0; }
char *ltoa(long value, char *result, int base);
char *ultoa(unsigned long value, char *result, int base);
char *itoa(int value, char *result, int base) { return ltoa(value, result, base); }
char *utoa(unsigned value, char *result, int base) { return ultoa(value, result, base); }
