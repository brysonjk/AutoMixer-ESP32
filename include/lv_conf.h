#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

// Memory settings
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (128U * 1024U)

// LVGL's object heap holds widget bookkeeping, not pixels, so it lives in PSRAM.
// Internal RAM is needed for the display's DMA bounce buffers, the draw buffer, and
// the WiFi driver; left as a static array this pool starves WiFi (ESP_ERR_NO_MEM).
#define LV_MEM_ADR 0
#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>
#define LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

// Display settings
#define LV_HOR_RES_MAX 800
#define LV_VER_RES_MAX 480
#define LV_DPI_DEF 130

// Enable features
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

// Font support
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_48 1

// Widget support
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CANVAS 1
#define LV_USE_CHECKBOX 1
#define LV_USE_DROPDOWN 1
#define LV_USE_IMG 1
#define LV_USE_LABEL 1
#define LV_USE_LINE 1
#define LV_USE_ROLLER 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_TEXTAREA 1
#define LV_USE_TABLE 1

// Theme support
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

// Animation
#define LV_USE_ANIMATION 1

#endif
