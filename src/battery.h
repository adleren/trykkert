#pragma once

/**
 * @brief Set battery charging to fast charge (100mA).
 *
 * @retval 0 if successful. Negative errno number on error.
 */
int battery_set_fast_charge(void);

/**
 * @brief Set battery charging to slow charge (50mA).
 *
 * @retval 0 if successful. Negative errno number on error.
 */
int battery_set_slow_charge(void);

/**
 * @brief Calculates the battery voltage using the ADC.
 *
 * @param[in] battery_volt Pointer where battery voltage is stored.
 *
 * @retval 0 if successful. Negative errno number on error.
 */
int battery_get_voltage(float *battery_volt);

/**
 * @brief Calculates the battery percentage using the battery voltage.
 *
 * @param[in] battery_percentage  Pointer where battery percentage is stored.
 *
 * @param[in] battery_volt Voltage used to calculate the percentage of how much energy is left in a 3.7V LiPo battery.
 *
 * @retval 0 if successful. Negative errno number on error.
 */
int battery_get_percentage(int *battery_percentage, float battery_volt);

/**
 * @brief Gets the current charging state
 *
 * @param[in] charge_state  Pointer where battery charge state is stored.
 *
 * @retval 0 if successful. Negative errno number on error.
 */
int battery_get_charge_state(int *charge_state);

/**
 * @brief Initialize the battery charging circuit.
 *
 * @retval 0 if successful. Negative errno number on error.
 */
int battery_init(void);
