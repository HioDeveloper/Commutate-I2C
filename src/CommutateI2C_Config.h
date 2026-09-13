#ifndef COMMUTATE_I2C_CONFIG_H
#define COMMUTATE_I2C_CONFIG_H

/* ============================================================================
 *  CommutateI2C — пользовательская конфигурация
 *  Этот файл ЕДИНСТВЕННЫЙ, который обычно правят под проект.
 * ==========================================================================*/

/* --- Функции --- */
#define COMMUTATE_I2C_USE_LCD       1   /* раскомментируйте/поставьте 0, если LCD нет */
#define COMMUTATE_I2C_USE_WATCHDOG  1
#define COMMUTATE_I2C_USE_LED       1
#define COMMUTATE_I2C_USE_SERIAL    1

/* --- Размеры (осторожно: RAM ограничена) --- */
#define COMMUTATE_I2C_MAX_MSG       16
#define COMMUTATE_I2C_MAX_SLAVES    16
#define COMMUTATE_I2C_QUEUE_SIZE     6

/* --- Тайминги по умолчанию --- */
#define COMMUTATE_I2C_SCAN_MS       5000
#define COMMUTATE_I2C_POLL_DELAY     3
#define COMMUTATE_I2C_AUTOSEND_MS 3000
#define COMMUTATE_I2C_RETRIES        3
#define COMMUTATE_I2C_RETRY_PAUSE    5

/* --- LCD (только для роли MASTER) --- */
#define COMMUTATE_I2C_LCD_ADDR     0x27
#define COMMUTATE_I2C_LCD_COLS       16
#define COMMUTATE_I2C_LCD_ROWS        2
#define COMMUTATE_I2C_LCD_SWAP_MS  4000

/* --- LED статус --- */
#define COMMUTATE_I2C_LED_PIN       13

/* --- Восстановление шины --- */
#define COMMUTATE_I2C_BUS_ERR_MAX    5

#endif