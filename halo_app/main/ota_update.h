#pragma once
/* HALO has no OTA yet. common/ui.c only needs ota_start_update(); this header shadows idf_app's. */
void ota_start_update(void);
