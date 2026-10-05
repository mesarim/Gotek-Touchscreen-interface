#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "ff.h"
#include "diskio.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  DSTATUS (*init)(BYTE pdrv);
  DSTATUS (*status)(BYTE pdrv);
  DRESULT (*read)(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count);
  DRESULT (*write)(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count);
  DRESULT (*ioctl)(BYTE pdrv, BYTE cmd, void *buff);
} ff_diskio_impl_t;
void ff_diskio_register(BYTE pdrv, const ff_diskio_impl_t *discio_impl);
#define ff_diskio_unregister(pdrv_) ff_diskio_register(pdrv_, NULL)
esp_err_t ff_diskio_get_drive(BYTE *out_pdrv);
#ifdef __cplusplus
}
#endif
