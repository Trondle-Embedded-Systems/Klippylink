#pragma once

#ifdef CONFIG_KLIPNODE_VARIANT_FULL

/**
 * Initialise ADC, LEDC PWM, and start the 10 Hz PID task.
 * Must be called after gatt_server_init().
 */
void heater_init(void);

/** Set the target temperature in °C. */
void heater_set_target(float temp_c);

/** Update PID gains at runtime. */
void heater_set_pid(float kp, float ki, float kd);

/** Immediately set PWM duty to 0. Call on BLE disconnect. */
void heater_emergency_off(void);

#endif /* CONFIG_KLIPNODE_VARIANT_FULL */
