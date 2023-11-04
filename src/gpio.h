#pragma once

#include <zephyr/types.h>

#define GPIO_SW_DEBOUNCE_MS 30
#define GPIO_SW_LONGPRESS_MS 5000

typedef void (*button_event_handler_t)(uint8_t button_mask);

int gpio_init(button_event_handler_t handler);
int gpio_status_led_on(void);
int gpio_status_led_off(void);
int gpio_status_led_toggle(void);
