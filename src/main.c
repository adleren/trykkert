#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/settings/settings.h>

#include "battery.h"
#include "bas.h"
#include "hid.h"
#include "gpio.h"

#include <zephyr/logging/log.h>
#define LOG_MODULE_NAME app
LOG_MODULE_REGISTER(LOG_MODULE_NAME);


/* callbacks & services */
static struct k_work_delayable blink_work;
static struct k_work_delayable battery_update_work;

static float battery_voltage;
static int battery_percentage;
static int battery_charge_state;

static uint8_t btn_state = 0;

static void button_handler(uint8_t button_mask)
{
    int err;

    LOG_INF("Btn state: %x\n", button_mask);
    if (button_mask == btn_state) {
        return; // ignore if state is unchanged
    }
    btn_state = button_mask;

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

static void connection_changed_handler(uint8_t state)
{
    if (state == 1) {
        gpio_status_led_off();
        k_work_cancel_delayable(&blink_work);
    } else {
        k_work_schedule(&blink_work, K_NO_WAIT);
    }
}

static void battery_update(struct k_work *work)
{
    int err;

    battery_get_voltage(&battery_voltage);
    battery_get_percentage(&battery_percentage, battery_voltage);
    bas_set_battery_level(battery_percentage);

    battery_get_charge_state(&battery_charge_state);
    bas_set_charge_status(battery_charge_state);

    err = bas_notify();
    if (err) {
        LOG_ERR("bas_notify failed with rc = %d\n", err);
    }

    k_work_reschedule(&battery_update_work, K_SECONDS(10));
}

static void blink(struct k_work *work)
{
    if (is_advertising()) {
        gpio_status_led_toggle();
    } else {
        gpio_status_led_off();
    }
    k_work_schedule(&blink_work, K_MSEC(500));
}


/* main task */
int main(void)
{
    int err;

    k_sleep(K_MSEC(3000)); // boot cooldown

    LOG_INF("Starting Bluetooth Peripheral HIDS keyboard example\n");

    battery_init();

    err = gpio_init(button_handler);
    if (err) {
        LOG_ERR("Failed to initialize GPIO (err: %d)\n", err);
    }

    hid_init(connection_changed_handler);

    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    LOG_INF("Bluetooth initialized\n");

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_load();
    }

    k_work_init_delayable(&battery_update_work, battery_update);
    k_work_schedule(&battery_update_work, K_NO_WAIT);

    k_work_init_delayable(&blink_work, blink);
    k_work_schedule(&blink_work, K_NO_WAIT);

    advertising_start();

    // idle loop
    for (;;) {
        k_sleep(K_FOREVER);
    }
}