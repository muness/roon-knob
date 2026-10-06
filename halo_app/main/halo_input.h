#pragma once
/* HALO V2 physical input: rotary encoder (GPIO6/GPIO38) and CST816 touch on I2C (SDA48/SCL45).
 * A polling task produces controller actions; halo_input_drain() dispatches them on the UI loop. */
void halo_input_start(void);
void halo_input_drain(void);
