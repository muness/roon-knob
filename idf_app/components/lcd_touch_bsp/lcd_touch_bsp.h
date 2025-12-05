#ifndef LCD_TOUCH_BSP_H
#define LCD_TOUCH_BSP_H

#ifdef __cplusplus
extern "C" {
#endif
void lcd_touch_init(void);
uint8_t tpGetCoordinates(uint16_t *x, uint16_t *y);
#ifdef __cplusplus
}
#endif

#endif
