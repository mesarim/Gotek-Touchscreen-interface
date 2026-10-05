#pragma once
#include <stdint.h>
typedef struct { uint32_t flags; int slot; int max_freq_khz; } sdmmc_host_t;
#define SDMMC_HOST_FLAG_1BIT (1<<0)
#define SDMMC_HOST_FLAG_4BIT (1<<1)
#define SDMMC_HOST_DEFAULT() (sdmmc_host_t){ SDMMC_HOST_FLAG_4BIT, 1, 20000 }
typedef struct { uint32_t capacity; uint32_t sector_size; } sdmmc_csd_t;
typedef struct { char name[8]; } sdmmc_cid_t;
typedef struct sdmmc_card_s { sdmmc_host_t host; uint32_t ocr; sdmmc_cid_t cid; sdmmc_csd_t csd; uint32_t max_freq_khz; int real_freq_khz; uint32_t is_mem; } sdmmc_card_t;
#define SDMMC_FREQ_DEFAULT 20000
#define SDMMC_FREQ_HIGHSPEED 40000
#define SDMMC_FREQ_PROBING 400
#define SDMMC_FREQ_52M 52000
#define SDMMC_FREQ_26M 26000
#define SD_OCR_SDHC_CAP (1<<30)
