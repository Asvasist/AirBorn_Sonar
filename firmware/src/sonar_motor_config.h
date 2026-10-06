#ifndef SONAR_MOTOR_CONFIG_H
#define SONAR_MOTOR_CONFIG_H

/* Pololu Tic 36v4 on PS I2C1 (MIO 12/13). */
#define SONAR_MOTOR_ADDRESS        0x0eU
#define SONAR_MOTOR_I2C_INPUT_HZ   111111115U
#define SONAR_MOTOR_I2C_HZ         20000U
#define SONAR_MOTOR_I2C_TIMEOUT_MS 100U
#define SONAR_MOTOR_SPEED          30000000U /* 3000 microsteps/s; preserve Tic step mode. */
#define SONAR_MOTOR_ACCELERATION   300000U   /* 3000 microsteps/s^2. */
#define SONAR_MOTOR_DECELERATION   300000U
#define SONAR_MOTOR_KEEPALIVE_MS   250U
#define SONAR_MOTOR_WATCHDOG_MS    1000U /* Must match the Tic Control Center setting. */
#endif
