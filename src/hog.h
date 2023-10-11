
#pragma once

#include <zephyr/types.h>

#include <assert.h>


#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define BASE_USB_HID_SPEC_VERSION 0x0101

#define OUTPUT_REPORT_MAX_LEN            1
#define INPUT_REP_KEYS_REF_ID            0
#define OUTPUT_REP_KEYS_REF_ID           0
#define MODIFIER_KEY_POS                 0
#define SCAN_CODE_POS                    2
#define KEYS_MAX_LEN                     (INPUT_REPORT_KEYS_MAX_LEN - SCAN_CODE_POS)

// #define ADV_STATUS_LED DK_LED1
// #define CON_STATUS_LED DK_LED2
// #define LED_CAPS_LOCK  DK_LED3
// #define NFC_LED	       DK_LED4
// #define KEY_TEXT_MASK  DK_BTN1_MSK
// #define KEY_SHIFT_MASK DK_BTN2_MSK
// #define KEY_ADV_MASK   DK_BTN4_MSK

/* ********************* */
/* Buttons configuration */

/* Note: The configuration below is the same as BOOT mode configuration
 * This simplifies the code as the BOOT mode is the same as REPORT mode.
 * Changing this configuration would require separate implementation of
 * BOOT mode report generation.
 */
#define KEY_CTRL_CODE_MIN 224 /* Control key codes - required 8 of them */
#define KEY_CTRL_CODE_MAX 231 /* Control key codes - required 8 of them */
#define KEY_CODE_MIN      0   /* Normal key codes */
#define KEY_CODE_MAX      101 /* Normal key codes */
#define KEY_PRESS_MAX     6   /* Maximum number of non-control keys
                   * pressed simultaneously
                   */

/* Number of bytes in key report
 *
 * 1B - control keys
 * 1B - reserved
 * rest - non-control keys
 */
#define INPUT_REPORT_KEYS_MAX_LEN (1 + 1 + KEY_PRESS_MAX)

/* Current report map construction requires exactly 8 buttons */
BUILD_ASSERT((KEY_CTRL_CODE_MAX - KEY_CTRL_CODE_MIN) + 1 == 8);

/* OUT report internal indexes.
 *
 * This is a position in internal report table and is not related to
 * report ID.
 */
enum {
    OUTPUT_REP_KEYS_IDX = 0
};

/* INPUT report internal indexes.
 *
 * This is a position in internal report table and is not related to
 * report ID.
 */
enum {
    INPUT_REP_KEYS_IDX = 0
};

void hid_init(void);
void advertising_start(void);
bool is_advertising();
void bas_notify(void);
