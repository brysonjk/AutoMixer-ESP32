/* Browser simulator: one ordinary heap. lv_conf.h asks for PSRAM; plain malloc serves.
   Plain C: LVGL (C) includes this through lv_conf.h. */
#pragma once
#include <stddef.h>
#include <stdlib.h>
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_8BIT (1 << 2)
static inline void* heap_caps_malloc(size_t size, unsigned caps) { (void)caps; return malloc(size); }
static inline size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 0; }
static inline size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return 0; }
