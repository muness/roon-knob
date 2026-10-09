#pragma once
/* HALO V2 rotary encoder (A=GPIO38, B=GPIO6). Touch is owned by platform_display_halo.c (LVGL pointer).
 * A polling task decodes the quadrature into detent deltas; halo_input_drain() coalesces them and
 * dispatches a Dial-style rotation physical event on the UI loop. */
void halo_input_start(void);
void halo_input_drain(void);
