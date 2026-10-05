#pragma once
#include "diskio_impl.h"
#include "driver/sdmmc_types.h"
#ifdef __cplusplus
extern "C" {
#endif
void ff_diskio_register_sdmmc(unsigned char pdrv, sdmmc_card_t *card);
BYTE ff_diskio_get_pdrv_card(const sdmmc_card_t *card);
#ifdef __cplusplus
}
#endif
