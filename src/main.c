#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "hog.h"

#include <zephyr/logging/log.h>
#define LOG_MODULE_NAME app
LOG_MODULE_REGISTER(LOG_MODULE_NAME);


static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS,
        (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)
    ),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL,
        BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL),
        BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err) {
        LOG_ERR("Failed to connect to %s (%u)\n", addr, err);
        return;
    }

    LOG_INF("Connected %s\n", addr);

    int ret = bt_conn_set_security(conn, BT_SECURITY_L2); 
    if (ret) {
        LOG_ERR("Failed to set security (err: %d)\n", ret);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Disconnected from %s (reason 0x%02x)\n", addr, reason);
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (!err) {
        LOG_INF("Security changed: %s level %u\n", addr, level);
    } else {
        LOG_ERR("Security failed: %s level %u err %d\n", addr, level, err);
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
};

static void bt_ready(int err)
{
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)\n", err);
        return;
    }

    LOG_INF("Bluetooth initialized\n");

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_load();
    }

    // ! uncomment to fix trouble
    bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY);

    hog_init();

    err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)\n", err);
        return;
    }

    LOG_INF("Advertising successfully started\n");
}


int main(void)
{
    int err;

    k_sleep(K_MSEC( 1000 )); // startup cooldown to allow logging

    err = bt_enable(bt_ready);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    hog_button_loop();
    return 0;
}