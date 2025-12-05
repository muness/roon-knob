#ifndef CST816_H
#define CST816_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize CST816 touch controller
 *
 * Initializes I2C bus and CST816 touch controller.
 * Must be called before using getTouch().
 *
 * @return true on success, false on failure
 */
bool cst816_init(void);

/**
 * @brief Read touch coordinates from CST816
 *
 * @param x Pointer to store X coordinate (0-359)
 * @param y Pointer to store Y coordinate (0-359)
 * @return true if touch detected, false if no touch
 */
bool cst816_get_touch(uint16_t *x, uint16_t *y);

#ifdef __cplusplus
}
#endif

#endif // CST816_H
