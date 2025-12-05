#include <stdio.h>
#include "lcd_touch_bsp.h"
#include "i2c_bsp.h"

void lcd_touch_init(void) {
  uint8_t data = 0x00;
  ESP_ERROR_CHECK(i2c_write_buff(disp_touch_dev_handle, 0x00, &data, 1));
}
uint8_t tpGetCoordinates(uint16_t *x, uint16_t *y) {
  uint8_t GetNum = 0;
  uint8_t data[7] = {0};
  i2c_read_buff(disp_touch_dev_handle, 0x00, data, 7);
  GetNum = data[2];
  if (GetNum) {
    *x = ((uint16_t)(data[3] & 0x0f) << 8) + (uint16_t)data[4];
    *y = ((uint16_t)(data[5] & 0x0f) << 8) + (uint16_t)data[6];
    return 1;
  }
  return 0;
}