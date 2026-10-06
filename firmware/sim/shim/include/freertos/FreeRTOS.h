#pragma once
#include <stdint.h>
#include <stddef.h>
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
#define portMAX_DELAY 0xffffffffUL
#define portTICK_PERIOD_MS 1
#define portTICK_RATE_MS 1
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define configMAX_PRIORITIES 25
#define tskIDLE_PRIORITY 0
#define portYIELD_FROM_ISR(...) do{}while(0)
#define portENTER_CRITICAL(m) do{}while(0)
#define portEXIT_CRITICAL(m) do{}while(0)
#define portMUX_INITIALIZER_UNLOCKED 0
typedef int portMUX_TYPE;
