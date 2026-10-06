#pragma once
#include "sdmmc_types.h"
#include "../esp_err.h"
#define SDMMC_HOST_SLOT_1 1
#include "gpio.h"
typedef struct { gpio_num_t clk, cmd, d0, d1, d2, d3, d4, d5, d6, d7, cd, wp; uint8_t width; uint32_t flags; } sdmmc_slot_config_t;
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP (1<<0)
#define SDMMC_SLOT_CONFIG_DEFAULT() (sdmmc_slot_config_t){ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 4, 0 }
#define SDMMC_HOST_SLOT_0 0
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t sdmmc_host_init(void);
esp_err_t sdmmc_host_init_slot(int slot, const sdmmc_slot_config_t *cfg);
esp_err_t sdmmc_host_deinit(void);
esp_err_t sdmmc_card_init(const sdmmc_host_t *host, sdmmc_card_t *out_card);
esp_err_t sdmmc_host_set_card_clk(int slot, uint32_t freq_khz);
esp_err_t sdmmc_host_get_real_freq(int slot, int *real_freq_khz);
#ifdef __cplusplus
}
#endif
