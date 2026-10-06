#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#define MALLOC_CAP_EXEC (1<<0)
#define MALLOC_CAP_32BIT (1<<1)
#define MALLOC_CAP_8BIT (1<<2)
#define MALLOC_CAP_DMA (1<<3)
#define MALLOC_CAP_SPIRAM (1<<10)
#define MALLOC_CAP_INTERNAL (1<<11)
#define MALLOC_CAP_DEFAULT (1<<12)
#define MALLOC_CAP_IRAM_8BIT (1<<13)
#define MALLOC_CAP_RETENTION (1<<14)
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*esp_alloc_failed_hook_t)(size_t size, uint32_t caps, const char *function_name);
void *heap_caps_malloc(size_t size, uint32_t caps);
void *heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void *heap_caps_realloc(void *p, size_t size, uint32_t caps);
void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps);
void heap_caps_free(void *p);
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);
size_t heap_caps_get_total_size(uint32_t caps);
size_t heap_caps_get_minimum_free_size(uint32_t caps);
void heap_caps_malloc_extmem_enable(size_t limit);
esp_err_t heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t cb);
#ifdef __cplusplus
}
#endif
#include "esp_err.h"
