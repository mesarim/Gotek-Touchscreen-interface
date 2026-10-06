#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
typedef struct { int type; int subtype; uint32_t address; uint32_t size; char label[17]; bool encrypted; } esp_partition_t;
typedef uint32_t esp_ota_handle_t;
#ifdef __cplusplus
extern "C" {
#endif
const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *start_from);
const esp_partition_t *esp_ota_get_boot_partition(void);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
#ifdef __cplusplus
}
#endif
