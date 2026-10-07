// Browser simulator: GPIO calls from ValveController::holdClosed() are no-ops.
#pragma once
#include <cstdint>
typedef int gpio_num_t;
enum { GPIO_MODE_OUTPUT = 2, GPIO_PULLUP_DISABLE = 0, GPIO_PULLDOWN_DISABLE = 0, GPIO_INTR_DISABLE = 0 };
struct gpio_config_t {
    uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
    int pull_down_en;
    int intr_type;
};
inline int gpio_set_level(gpio_num_t, uint32_t) { return 0; }
inline int gpio_config(const gpio_config_t*) { return 0; }
