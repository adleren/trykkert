#include "hog.h"

#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/services/dis.h>
#include <bluetooth/services/hids.h>

#include <soc.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

#include <zephyr/logging/log.h>
#define LOG_MODULE_NAME hog
LOG_MODULE_REGISTER(LOG_MODULE_NAME);


/* HIDS instance. */
BT_HIDS_DEF(
    hids_obj,
    OUTPUT_REPORT_MAX_LEN,
    INPUT_REPORT_KEYS_MAX_LEN
);

static volatile bool is_adv;

static hid_connection_changed_t connection_changed_cb;

static const struct bt_data ad[] = {
    BT_DATA_BYTES(
        BT_DATA_GAP_APPEARANCE,
        (CONFIG_BT_DEVICE_APPEARANCE >> 0) & 0xff,
        (CONFIG_BT_DEVICE_APPEARANCE >> 8) & 0xff
    ),
    BT_DATA_BYTES(
        BT_DATA_FLAGS,
        (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)
    ),
    BT_DATA_BYTES(
        BT_DATA_UUID16_ALL,
        BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL),
        BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)
    ),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static struct conn_mode {
    struct bt_conn *conn;
    bool in_boot_mode;
} conn_mode;

static struct keyboard_state {
    uint8_t ctrl_keys_state;
    uint8_t keys_state[KEY_PRESS_MAX];
} hid_keyboard_state;


void advertising_start(void)
{
    int err;
    struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
        (BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_ONE_TIME),
        BT_GAP_ADV_FAST_INT_MIN_2,
        BT_GAP_ADV_FAST_INT_MAX_2,
        NULL
    );

    err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        if (err == -EALREADY) {
            LOG_WRN("Advertising continued\n");
        } else {
            LOG_ERR("Advertising failed to start (err %d)\n", err);
        }
        return;
    }
    is_adv = true;
    LOG_INF("Advertising successfully started\n");
}


bool is_advertising(void)
{
    return is_adv;
}


static void connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err) {
        LOG_ERR("Failed to connect to %s (%u)\n", addr, err);
        if (connection_changed_cb) {
            connection_changed_cb(0);
        }
        return;
    }

    LOG_INF("Connected %s\n", addr);

    err = bt_hids_connected(&hids_obj, conn);

    if (err) {
        LOG_ERR("Failed to notify HID service about connection\n");
        if (connection_changed_cb) {
            connection_changed_cb(0);
        }
        return;
    }

    if (!conn_mode.conn) {
        conn_mode.conn = conn;
        conn_mode.in_boot_mode = false;
    }

    is_adv = false;
    if (connection_changed_cb) {
        connection_changed_cb(1);
    }
}


static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    int err;

    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Disconnected from %s (reason %u)\n", addr, reason);

    err = bt_hids_disconnected(&hids_obj, conn);
    if (err) {
        LOG_ERR("Failed to notify HID service about disconnection\n");
        if (connection_changed_cb) {
            connection_changed_cb(0);
        }
    }

    if (conn_mode.conn) {
        conn_mode.conn = NULL;
    }

    advertising_start();
    if (connection_changed_cb) {
        connection_changed_cb(0);
    }
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


static void hids_outp_rep_handler(struct bt_hids_rep *rep, struct bt_conn *conn, bool write)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (!write) {
        LOG_INF("Output report read\n");
        return;
    };

    LOG_INF("Output report has been received %s\n", addr);
}


static void hids_boot_kb_outp_rep_handler(struct bt_hids_rep *rep, struct bt_conn *conn, bool write)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (!write) {
        LOG_INF("Output report read\n");
        return;
    };

    LOG_INF("Boot Keyboard Output report has been received %s\n", addr);
}


static void hids_pm_evt_handler(enum bt_hids_pm_evt evt, struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    switch (evt) {
    case BT_HIDS_PM_EVT_BOOT_MODE_ENTERED:
        LOG_INF("Boot mode entered %s\n", addr);
        conn_mode.in_boot_mode = true;
        break;

    case BT_HIDS_PM_EVT_REPORT_MODE_ENTERED:
        LOG_INF("Report mode entered %s\n", addr);
        conn_mode.in_boot_mode = false;
        break;

    default:
        break;
    }
}


void hid_init(hid_connection_changed_t cb)
{
    int err;

    connection_changed_cb = cb;

    struct bt_hids_init_param    hids_init_obj = { 0 };
    struct bt_hids_inp_rep       *hids_inp_rep;
    struct bt_hids_outp_feat_rep *hids_outp_rep;

    static const uint8_t report_map[] = {
        0x05, 0x01,       /* Usage Page (Generic Desktop) */
        0x09, 0x06,       /* Usage (Keyboard) */
        0xa1, 0x01,       /* Collection (Application) */

        /* Keys */
#if INPUT_REP_KEYS_REF_ID
        0x85, INPUT_REP_KEYS_REF_ID,
#endif
        0x05, 0x07,       /* Usage Page (Key Codes) */
        0x19, 0xe0,       /* Usage Minimum (224) */
        0x29, 0xe7,       /* Usage Maximum (231) */
        0x15, 0x00,       /* Logical Minimum (0) */
        0x25, 0x01,       /* Logical Maximum (1) */
        0x75, 0x01,       /* Report Size (1) */
        0x95, 0x08,       /* Report Count (8) */
        0x81, 0x02,       /* Input (Data, Variable, Absolute) */

        0x95, 0x01,       /* Report Count (1) */
        0x75, 0x08,       /* Report Size (8) */
        0x81, 0x01,       /* Input (Constant) reserved byte(1) */

        0x95, 0x06,       /* Report Count (6) */
        0x75, 0x08,       /* Report Size (8) */
        0x15, 0x00,       /* Logical Minimum (0) */
        0x25, 0x65,       /* Logical Maximum (101) */
        0x05, 0x07,       /* Usage Page (Key codes) */
        0x19, 0x00,       /* Usage Minimum (0) */
        0x29, 0x65,       /* Usage Maximum (101) */
        0x81, 0x00,       /* Input (Data, Array) Key array(6 bytes) */

        /* LED */
#if OUTPUT_REP_KEYS_REF_ID
        0x85, OUTPUT_REP_KEYS_REF_ID,
#endif
        0x95, 0x05,       /* Report Count (5) */
        0x75, 0x01,       /* Report Size (1) */
        0x05, 0x08,       /* Usage Page (Page# for LEDs) */
        0x19, 0x01,       /* Usage Minimum (1) */
        0x29, 0x05,       /* Usage Maximum (5) */
        0x91, 0x02,       /* Output (Data, Variable, Absolute), */
        0x95, 0x01,       /* Report Count (1) */
        0x75, 0x03,       /* Report Size (3) */
        0x91, 0x01,       /* Output (Data, Variable, Absolute), */

        0xC0              /* End Collection (Application) */
    };

    hids_init_obj.rep_map.data = report_map;
    hids_init_obj.rep_map.size = sizeof(report_map);

    hids_init_obj.info.bcd_hid = BASE_USB_HID_SPEC_VERSION;
    hids_init_obj.info.b_country_code = 0x00;
    hids_init_obj.info.flags = (BT_HIDS_REMOTE_WAKE | BT_HIDS_NORMALLY_CONNECTABLE);

    hids_inp_rep = &hids_init_obj.inp_rep_group_init.reports[INPUT_REP_KEYS_IDX];
    hids_inp_rep->size = INPUT_REPORT_KEYS_MAX_LEN;
    hids_inp_rep->id = INPUT_REP_KEYS_REF_ID;
    hids_init_obj.inp_rep_group_init.cnt++;

    hids_outp_rep = &hids_init_obj.outp_rep_group_init.reports[OUTPUT_REP_KEYS_IDX];
    hids_outp_rep->size = OUTPUT_REPORT_MAX_LEN;
    hids_outp_rep->id = OUTPUT_REP_KEYS_REF_ID;
    hids_outp_rep->handler = hids_outp_rep_handler;
    hids_init_obj.outp_rep_group_init.cnt++;

    hids_init_obj.is_kb = true;
    hids_init_obj.boot_kb_outp_rep_handler = hids_boot_kb_outp_rep_handler;
    hids_init_obj.pm_evt_handler = hids_pm_evt_handler;

    err = bt_hids_init(&hids_obj, &hids_init_obj);
    __ASSERT(err == 0, "HIDS initialization failed\n");
}


static int key_report_con_send(const struct keyboard_state *state, bool boot_mode, struct bt_conn *conn)
{
    int err = 0;
    uint8_t  data[INPUT_REPORT_KEYS_MAX_LEN];
    uint8_t *key_data;
    const uint8_t *key_state;
    size_t n;

    data[0] = state->ctrl_keys_state;
    data[1] = 0;
    key_data = &data[2];
    key_state = state->keys_state;

    for (n = 0; n < KEY_PRESS_MAX; ++n) {
        *key_data++ = *key_state++;
    }
    if (boot_mode) {
        err = bt_hids_boot_kb_inp_rep_send(&hids_obj, conn, data, sizeof(data), NULL);
    } else {
        err = bt_hids_inp_rep_send(&hids_obj, conn, INPUT_REP_KEYS_IDX, data, sizeof(data), NULL);
    }
    return err;
}


static int key_report_send(void)
{
    if (conn_mode.conn) {
        int err;

        err = key_report_con_send(&hid_keyboard_state, conn_mode.in_boot_mode, conn_mode.conn);
        if (err) {
            LOG_ERR("Key report send error: %d\n", err);
            return err;
        }
    }
    return 0;
}


static uint8_t button_ctrl_code(uint8_t key)
{
    if (KEY_CTRL_CODE_MIN <= key && key <= KEY_CTRL_CODE_MAX) {
        return (uint8_t)(1U << (key - KEY_CTRL_CODE_MIN));
    }
    return 0;
}


static int hid_kbd_state_key_set(uint8_t key)
{
    uint8_t ctrl_mask = button_ctrl_code(key);

    if (ctrl_mask) {
        hid_keyboard_state.ctrl_keys_state |= ctrl_mask;
        return 0;
    }
    for (size_t i = 0; i < KEY_PRESS_MAX; ++i) {
        if (hid_keyboard_state.keys_state[i] == 0) {
            hid_keyboard_state.keys_state[i] = key;
            return 0;
        }
    }
    /* All slots busy */
    return -EBUSY;
}


static int hid_kbd_state_key_clear(uint8_t key)
{
    uint8_t ctrl_mask = button_ctrl_code(key);

    if (ctrl_mask) {
        hid_keyboard_state.ctrl_keys_state &= ~ctrl_mask;
        return 0;
    }
    for (size_t i = 0; i < KEY_PRESS_MAX; ++i) {
        if (hid_keyboard_state.keys_state[i] == key) {
            hid_keyboard_state.keys_state[i] = 0;
            return 0;
        }
    }
    /* Key not found */
    return -EINVAL;
}


int hid_key_changed(uint8_t button_mask)
{
    if (button_mask & (uint8_t)(1U << 0)) {
        hid_kbd_state_key_set(KEY_CODE_SW0);
    } else {
        hid_kbd_state_key_clear(KEY_CODE_SW0);
    }

    if (button_mask & (uint8_t)(1U << 1)) {
        hid_kbd_state_key_set(KEY_CODE_SW1);
    } else {
        hid_kbd_state_key_clear(KEY_CODE_SW1);
    }

    return key_report_send();
}
