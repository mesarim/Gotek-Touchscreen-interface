// sim: SD_MMC on the virtual card (see SD_MMC.h)
#include <Arduino.h>
#include <SD_MMC.h>
#include "vfs_api.h"
#include "ff.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"

extern int g_sim_sd_pdrv;
static sdmmc_card_t s_card;
static FATFS s_fs;

using namespace fs;
SDMMCFS::SDMMCFS(FSImplPtr impl) : FS(impl), _card(nullptr) {}

bool SDMMCFS::begin(const char *mountpoint, bool mode1bit, bool, int, uint8_t) {
  if (_card) return true;
  if (!js_disk_present()) return false;
  BYTE pdrv = 0xFF;
  if (ff_diskio_get_drive(&pdrv) != ESP_OK) return false;
  memset(&s_card, 0, sizeof s_card);
  s_card.ocr = SD_OCR_SDHC_CAP; s_card.csd.capacity = js_disk_sectors(); s_card.csd.sector_size = 512;
  s_card.max_freq_khz = 40000; s_card.real_freq_khz = 40000; s_card.is_mem = 1;
  strcpy(s_card.cid.name, "SIMSD");
  ff_diskio_register_sdmmc(pdrv, &s_card);
  char drv[4] = {(char)('0' + pdrv), ':', 0, 0};
  if (f_mount(&s_fs, drv, 1) != FR_OK) { ff_diskio_unregister(pdrv); return false; }
  _card = &s_card; _pdrv = pdrv; _mode1bit = mode1bit;
  g_sim_sd_pdrv = pdrv;
  _impl->mountpoint(mountpoint);
  return true;
}
void SDMMCFS::end() {
  if (!_card) return;
  char drv[4] = {(char)('0' + _pdrv), ':', 0, 0};
  f_mount(NULL, drv, 0);
  ff_diskio_unregister(_pdrv);
  _impl->mountpoint(NULL);
  _card = nullptr; g_sim_sd_pdrv = -1; _pdrv = 0xFF;
}
sdcard_type_t SDMMCFS::cardType() { return _card ? CARD_SDHC : CARD_NONE; }
uint64_t SDMMCFS::cardSize() { return _card ? (uint64_t)js_disk_sectors() * 512ULL : 0; }
int SDMMCFS::sectorSize() { return _card ? 512 : 0; }
int SDMMCFS::numSectors() { return _card ? (int)js_disk_sectors() : 0; }
uint64_t SDMMCFS::totalBytes() {
  if (!_card) return 0; FATFS *fsinfo; DWORD fre_clust; char drv[4] = {(char)('0' + _pdrv), ':', 0, 0};
  if (f_getfree(drv, &fre_clust, &fsinfo) != FR_OK) return 0;
  return (uint64_t)(fsinfo->n_fatent - 2) * fsinfo->csize * 512ULL;
}
uint64_t SDMMCFS::usedBytes() {
  if (!_card) return 0; FATFS *fsinfo; DWORD fre_clust; char drv[4] = {(char)('0' + _pdrv), ':', 0, 0};
  if (f_getfree(drv, &fre_clust, &fsinfo) != FR_OK) return 0;
  return (uint64_t)((fsinfo->n_fatent - 2) - fre_clust) * fsinfo->csize * 512ULL;
}
// as on the board: raw sector access goes through the card's FatFs drive (= the SD guard once installed)
bool SDMMCFS::readRAW(uint8_t *buffer, uint32_t sector) { return _card && disk_read(_pdrv, buffer, sector, 1) == RES_OK; }
bool SDMMCFS::writeRAW(uint8_t *buffer, uint32_t sector) { return _card && disk_write(_pdrv, buffer, sector, 1) == RES_OK; }

fs::SDMMCFS SD_MMC = fs::SDMMCFS(FSImplPtr(new VFSImpl()));
