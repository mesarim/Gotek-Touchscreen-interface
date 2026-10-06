#pragma once
#include "esp32-hal-log.h"
#define ESP_LOGE(t,...) do{}while(0)
#define ESP_LOGW(t,...) do{}while(0)
#define ESP_LOGI(t,...) do{}while(0)
#define ESP_LOGD(t,...) do{}while(0)
#define ESP_LOGV(t,...) do{}while(0)
typedef enum { ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE } esp_log_level_t;
static inline void esp_log_level_set(const char *t, esp_log_level_t l){ (void)t; (void)l; }
#include <stdarg.h>
typedef int (*vprintf_like_t)(const char *, va_list);
#ifdef __cplusplus
extern "C" {
#endif
vprintf_like_t esp_log_set_vprintf(vprintf_like_t func);
#ifdef __cplusplus
}
#endif
