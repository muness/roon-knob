#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
typedef int nvs_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NOT_FOUND 1
#define ESP_ERR_NVS_INVALID_LENGTH 2
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define ESP_RST_PANIC 4
#define ESP_RST_INT_WDT 5
#define ESP_RST_TASK_WDT 6
#define ESP_RST_WDT 7
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_COREDUMP 3
#define CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH 1
#define ESP_IDF_VERSION_MAJOR 6
#define CONFIG_IDF_TARGET_ARCH_XTENSA 1
typedef struct {char version[32];} esp_app_desc_t;
typedef struct {int dummy;} esp_partition_t;
typedef struct {char exc_task[16];uint32_t exc_pc;struct {uint32_t depth,bt[16];} exc_bt_info;char app_elf_sha256[65];} esp_core_dump_summary_t;
int nvs_open(const char*,int,nvs_handle_t*);int nvs_get_blob(nvs_handle_t,const char*,void*,size_t*);int nvs_set_blob(nvs_handle_t,const char*,const void*,size_t);int nvs_commit(nvs_handle_t);void nvs_close(nvs_handle_t);
int esp_reset_reason(void);const esp_app_desc_t *esp_app_get_description(void);
const esp_partition_t *esp_partition_find_first(int,int,const char*);
int esp_core_dump_image_check(void);int esp_core_dump_get_summary(esp_core_dump_summary_t*);int esp_core_dump_image_erase(void);
