#pragma once
#include "../esp_err.h"
#include <stdint.h>
typedef enum { SPI1_HOST=0, SPI2_HOST=1, SPI3_HOST=2 } spi_host_device_t;
#define SPI_DMA_CH_AUTO 3
typedef struct { int mosi_io_num, miso_io_num, sclk_io_num, quadwp_io_num, quadhd_io_num, data0_io_num, data1_io_num, data2_io_num, data3_io_num, max_transfer_sz; uint32_t flags; } spi_bus_config_t;
static inline esp_err_t spi_bus_initialize(spi_host_device_t h, const spi_bus_config_t *c, int dma){(void)h;(void)c;(void)dma;return 0;}
