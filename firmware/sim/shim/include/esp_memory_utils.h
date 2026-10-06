#pragma once
#include <stdbool.h>
static inline bool esp_ptr_external_ram(const void *p){ (void)p; return true; }
static inline bool esp_ptr_internal(const void *p){ (void)p; return false; }
static inline bool esp_ptr_dma_capable(const void *p){ (void)p; return true; }
