#pragma once
#include "driver/sdmmc_types.h"
#include "esp_err.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t sdmmc_read_sectors(sdmmc_card_t *card, void *dst, size_t start_sector, size_t sector_count);
esp_err_t sdmmc_write_sectors(sdmmc_card_t *card, const void *src, size_t start_sector, size_t sector_count);
esp_err_t sdmmc_get_status(sdmmc_card_t *card);
#ifdef __cplusplus
}
#endif
