#pragma once
#include "../esp_err.h"
typedef int gpio_num_t;
typedef enum { GPIO_PULLUP_ONLY, GPIO_PULLDOWN_ONLY, GPIO_PULLUP_PULLDOWN, GPIO_FLOATING } gpio_pull_mode_t;
#define GPIO_NUM_NC -1
#ifdef __cplusplus
extern "C" {
#endif
static inline esp_err_t gpio_pullup_en(gpio_num_t g){(void)g;return 0;}
static inline esp_err_t gpio_pullup_dis(gpio_num_t g){(void)g;return 0;}
static inline esp_err_t gpio_pulldown_en(gpio_num_t g){(void)g;return 0;}
static inline esp_err_t gpio_pulldown_dis(gpio_num_t g){(void)g;return 0;}
static inline esp_err_t gpio_set_pull_mode(gpio_num_t g, gpio_pull_mode_t m){(void)g;(void)m;return 0;}
static inline esp_err_t gpio_reset_pin(gpio_num_t g){(void)g;return 0;}
static inline int gpio_get_level(gpio_num_t g){(void)g;return 1;}
static inline esp_err_t gpio_set_level(gpio_num_t g, unsigned l){(void)g;(void)l;return 0;}
#ifdef __cplusplus
}
#endif
