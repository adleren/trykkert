#pragma once

#include <zephyr/types.h>

#define GPIO_SW_DEBOUNCE_MS 15
#define GPIO_SW_LONGPRESS_MS 3000

typedef void (*button_event_handler_t)(uint8_t button_mask);

int gpio_init(button_event_handler_t handler);
