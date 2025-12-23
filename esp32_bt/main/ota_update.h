#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Initialize OTA subsystem
 * 
 * Checks current running partition and prepares for updates.
 */
void ota_update_init(void);

/**
 * @brief Start OTA update from UART
 * 
 * Receives firmware image via UART from ESP32-S3.
 * The S3 will first send the total image size, then stream the data.
 * 
 * @param image_size Total size of firmware image in bytes
 * @return true if OTA update succeeded and device will reboot
 *         false if OTA update failed
 */
bool ota_update_from_uart(uint32_t image_size);

/**
 * @brief Get current running partition info
 * 
 * @param partition_label Buffer to receive partition label (min 32 bytes)
 * @param partition_size Size of partition_label buffer
 */
void ota_get_running_partition(char *partition_label, size_t partition_size);

#endif // OTA_UPDATE_H
