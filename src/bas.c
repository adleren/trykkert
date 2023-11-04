#include "bas.h"

#include <errno.h>
#include <zephyr/init.h>
#include <zephyr/sys/__assert.h>
#include <stdbool.h>
#include <zephyr/types.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#define LOG_LEVEL CONFIG_BT_BAS_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bas);

static uint8_t battery_level = 100U;

static struct __packed battery_level_status {
    uint16_t power_state;
    uint8_t flags;
} battery_level_status;

static void blvl_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    LOG_INF("BAS Notifications %s", (value == BT_GATT_CCC_NOTIFY) ? "enabled" : "disabled");
}

static ssize_t read_blvl(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    uint8_t lvl8 = battery_level;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &lvl8, sizeof(lvl8));
}

static void blvl_status_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    LOG_INF("BAS Notifications %s", (value == BT_GATT_CCC_NOTIFY) ? "enabled" : "disabled");
}

static ssize_t read_blvl_status(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    struct __packed battery_level_status status = {
        .flags = battery_level_status.flags,
        .power_state = battery_level_status.power_state,
    };
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &status, sizeof(status));
}

BT_GATT_SERVICE_DEFINE(bas_extended,
    BT_GATT_PRIMARY_SERVICE(
        BT_UUID_BAS
    ),

    BT_GATT_CHARACTERISTIC(
        BT_UUID_BAS_BATTERY_LEVEL,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        read_blvl, NULL, &battery_level
    ),
    BT_GATT_CCC(
        blvl_ccc_cfg_changed,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE
    ),

    BT_GATT_CHARACTERISTIC(
        BT_UUID_BAS_BATTERY_LEVEL_STATUS,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ_ENCRYPT,
        read_blvl_status, NULL, &battery_level_status
    ),
    BT_GATT_CCC(
        blvl_status_ccc_cfg_changed,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE
    ),
);

static int bas_init(void)
{
    battery_level_status.flags = 0; // always 0
    battery_level_status.power_state = 0;
    battery_level_status.power_state |= (1U << 0); // battery assumed always present
    battery_level_status.power_state |= (2U << 1); // wired power source: unknown
    battery_level_status.power_state |= (0U << 3); // wireless power source: never
    battery_level_status.power_state |= (3U << 5); // battery charge state: inactive
    battery_level_status.power_state |= (0U << 7); // battery charge level: unknown
    return 0;
}

uint8_t bas_get_battery_level(void)
{
    return battery_level;
}

void bas_set_battery_level(uint8_t level)
{
    battery_level = level;
}

void bas_set_charge_status(int status)
{
    uint16_t power_state;

    power_state = 0;
    power_state |= (1U << 0); // battery assumed always present

    if ( status ) {
        power_state |= (1U << 1); // wired power source: connected
        power_state |= (1U << 5); // battery charge state: charging
        if (battery_level < 10) {
            power_state |= (3U << 7); // battery charge level: critical
        } else if (battery_level < 20) {
            power_state |= (2U << 7); // battery charge level: low
        } else {
            power_state |= (1U << 7); // battery charge level: good
        }
        power_state |= (1U << 9); // battery charge type: constant current
    } else {
        power_state |= (2U << 1); // wired power source: unknown
        power_state |= (2U << 5); // battery charge state: active discharge
        power_state |= (0U << 7); // battery charge level: not charging
        power_state |= (0U << 9); // battery charge type: not charging
    }

    battery_level_status.flags = 0;
    battery_level_status.power_state = power_state;
}

int bas_notify(void)
{
    int rc = 0;

    if (battery_level > 100U) {
        return -EINVAL;
    }

    // notify battery level
    rc |= bt_gatt_notify(
        NULL, bt_gatt_find_by_uuid(bas_extended.attrs, 0, BT_UUID_BAS_BATTERY_LEVEL),
        &battery_level, sizeof(battery_level)
    );

    // notify battery status (not working... not implemented on iOS???)
    rc |= bt_gatt_notify(
        NULL, bt_gatt_find_by_uuid(bas_extended.attrs, 0, BT_UUID_BAS_BATTERY_LEVEL_STATUS),
        &battery_level_status, sizeof(battery_level_status)
    );

    return rc == -ENOTCONN ? 0 : rc;
}

SYS_INIT(bas_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
