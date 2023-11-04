#pragma once

#include <stdint.h>

uint8_t bas_get_battery_level(void);
void bas_set_battery_level(uint8_t level);
void bas_set_charge_status(int status);
int bas_notify(void);
