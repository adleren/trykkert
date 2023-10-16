#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/settings/settings.h>

#include "hog.h"
#include "gpio.h"


#define ADV_LED_BLINK_INTERVAL 500


#include <zephyr/logging/log.h>
#define LOG_MODULE_NAME app
LOG_MODULE_REGISTER(LOG_MODULE_NAME);


static void button_handler(uint8_t button_mask)
{
    int err;

    LOG_INF("Btn state: %x\n", button_mask);

    if (button_mask & 0b100) {
        // long press detected
        bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY);
        gpio_status_led_off();
        advertising_start();
    } else {
        if (is_advertising()) {
            return;
        }

        if (button_mask) {
            gpio_status_led_on();
        } else {
            gpio_status_led_off();
        }

        err = hid_key_changed(button_mask);
        if (err) {
            LOG_ERR("Unable to update keys (err: %d)\n", err);
        }
    }
}


int main(void)
{
    int err;

    k_sleep(K_MSEC(3000)); // boot cooldown

    LOG_INF("Starting Bluetooth Peripheral HIDS keyboard example\n");

    err = gpio_init(button_handler);
    if (err) {
        LOG_ERR("Failed to initialize GPIO (err: %d)\n", err);
    }

    hid_init();

    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    LOG_INF("Bluetooth initialized\n");

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_load();
    }

    advertising_start();

    for (;;) {
        if (is_advertising()) {
            gpio_status_led_toggle();
        }
        k_sleep(K_MSEC(ADV_LED_BLINK_INTERVAL));

        /* Battery level simulation */
        bas_notify();
    }
}