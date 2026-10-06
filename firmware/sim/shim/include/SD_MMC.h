// sim: SD_MMC on a virtual card. The card is a disk image held by the web page (sectors on demand);
// FatFs (the real R0.15) runs on it through the same ff_diskio layer as on the board, so the SD guard,
// the raw FAT walker and the alias mount all see real sectors.
#pragma once
#include "FS.h"
#include "driver/sdmmc_types.h"
typedef enum { CARD_NONE, CARD_MMC, CARD_SD, CARD_SDHC, CARD_UNKNOWN } sdcard_type_t;
namespace fs {
class SDMMCFS : public FS {
protected:
  sdmmc_card_t *_card;
  uint8_t _pdrv = 0xFF;
  bool _mode1bit = false;
public:
  SDMMCFS(FSImplPtr impl);
  bool setPins(int clk, int cmd, int d0) { (void)clk; (void)cmd; (void)d0; return true; }
  bool setPins(int clk, int cmd, int d0, int d1, int d2, int d3) { (void)clk; (void)cmd; (void)d0; (void)d1; (void)d2; (void)d3; return true; }
  bool begin(const char *mountpoint = "/sdcard", bool mode1bit = false, bool format_if_mount_failed = false, int sdmmc_frequency = 40000, uint8_t maxOpenFiles = 5);
  void end();
  sdcard_type_t cardType();
  uint64_t cardSize();
  uint64_t totalBytes();
  uint64_t usedBytes();
  int sectorSize();
  int numSectors();
  bool readRAW(uint8_t *buffer, uint32_t sector);
  bool writeRAW(uint8_t *buffer, uint32_t sector);
};
}
extern fs::SDMMCFS SD_MMC;
