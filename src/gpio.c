#include "gpio.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/logging/log.h>
#define LOG_MODULE_NAME gpio
LOG_MODULE_REGISTER(LOG_MODULE_NAME);


#define SW0_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec sw0 = GPIO_DT_SPEC_GET(SW0_NODE, gpios);

#define SW1_NODE DT_ALIAS(sw1)
static const struct gpio_dt_spec sw1 = GPIO_DT_SPEC_GET(SW1_NODE, gpios);

static struct gpio_callback sw0_cb_data;
static struct gpio_callback sw1_cb_data;

static button_event_handler_t user_cb;

static struct k_work_delayable debounce_work;
static struct k_work_delayable longpress_work;


static void debounce_expired(struct k_work *work)
{
    ARG_UNUSED(work);

    uint8_t btn_mask = 0;
    
    if (gpio_pin_get_dt(&sw0)) {
        btn_mask |= 0b001;
    }
    if (gpio_pin_get_dt(&sw1)) {
        btn_mask |= 0b010;
    }

    if (btn_mask == 0b011) {
        k_work_reschedule(&longpress_work, K_MSEC(GPIO_SW_LONGPRESS_MS));
    }

    if (user_cb) {
        user_cb(btn_mask);
    }
}

static void longpress_expired(struct k_work *work)
{
    ARG_UNUSED(work);

    uint8_t btn_mask = 0;
    
    if (gpio_pin_get_dt(&sw0) && gpio_pin_get_dt(&sw1)) {
        btn_mask |= 0b111;

        if (user_cb) {
            user_cb(btn_mask);
        }
    }
}

static K_WORK_DELAYABLE_DEFINE(debounce_work, debounce_expired);
static K_WORK_DELAYABLE_DEFINE(longpress_work, longpress_expired);


void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    k_work_reschedule(&debounce_work, K_MSEC(GPIO_SW_DEBOUNCE_MS));
}


int gpio_init(button_event_handler_t handler)
{
    int err = -1;

    if (!handler) {
        return -EINVAL;
    }

    user_cb = handler;

    if (!device_is_ready(sw0.port)) {
        return -EIO;
    }
    if (!device_is_ready(sw1.port)) {
        return -EIO;
    }

    err = gpio_pin_configure_dt(&sw0, GPIO_INPUT);
    if (err) {
        return err;
    }
    err = gpio_pin_configure_dt(&sw1, GPIO_INPUT);
    if (err) {
        return err;
    }

    err = gpio_pin_interrupt_configure_dt(&sw0, GPIO_INT_EDGE_BOTH);
    if (err) {
        return err;
    }
    err = gpio_pin_interrupt_configure_dt(&sw1, GPIO_INT_EDGE_BOTH);
    if (err) {
        return err;
    }

    gpio_init_callback(&sw0_cb_data, button_pressed, BIT(sw0.pin));
    gpio_init_callback(&sw1_cb_data, button_pressed, BIT(sw1.pin));

    err = gpio_add_callback(sw0.port, &sw0_cb_data);
    if (err) {
        return err;
    }
    err = gpio_add_callback(sw1.port, &sw1_cb_data);
    if (err) {
        return err;
    }

    LOG_INF("Initialized GPIO\n");

    return 0;
}