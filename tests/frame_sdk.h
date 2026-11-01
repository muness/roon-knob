#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT -2
#define ESP_ERR_INVALID_STATE -3
#define I2C_NUM_0 0
#define I2C_CLK_SRC_DEFAULT 0
#define I2C_ADDR_BIT_LEN_7 0
#define GPIO_MODE_OUTPUT_OD 1
#define GPIO_MODE_INPUT_OUTPUT_OD 4
#define GPIO_MODE_INPUT 2
#define GPIO_MODE_OUTPUT 3
#define GPIO_PULLUP_ONLY 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define pdMS_TO_TICKS(x) (x)
#define SPI3_HOST 3
#define SPI_DMA_CH_AUTO 0
#define SPI_DEVICE_HALFDUPLEX 1
#define MALLOC_CAP_SPIRAM 1
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef struct { int i2c_port,sda_io_num,scl_io_num,clk_source,glitch_ignore_cnt; struct { bool enable_internal_pullup; } flags; } i2c_master_bus_config_t;
typedef struct {int dev_addr_length,device_address,scl_speed_hz;} i2c_device_config_t;
typedef struct {int intr_type,mode;uint64_t pin_bit_mask;int pull_down_en,pull_up_en;} gpio_config_t;
typedef void *spi_device_handle_t;
typedef struct {int miso_io_num,mosi_io_num,sclk_io_num,quadwp_io_num,quadhd_io_num,max_transfer_sz;} spi_bus_config_t;
typedef struct {int spics_io_num,clock_speed_hz,mode,queue_size,flags;} spi_device_interface_config_t;
typedef struct {int length;const void *tx_buffer;} spi_transaction_t;
int i2c_new_master_bus(const i2c_master_bus_config_t*,void**);
int i2c_master_bus_add_device(void*,const i2c_device_config_t*,void**);
int i2c_master_transmit_receive(void*,const uint8_t*,size_t,uint8_t*,size_t,int);
int i2c_master_transmit(void*,const uint8_t*,size_t,int);
int gpio_set_level(int,int);int gpio_get_level(int);int gpio_set_direction(int,int);int gpio_set_pull_mode(int,int);int gpio_reset_pin(int);int gpio_config(const gpio_config_t*);
void esp_rom_delay_us(int);void vTaskDelay(int);int64_t esp_timer_get_time(void);
int spi_bus_initialize(int,const spi_bus_config_t*,int);int spi_bus_add_device(int,const spi_device_interface_config_t*,void**);int spi_device_polling_transmit(void*,spi_transaction_t*);
void *heap_caps_calloc(size_t,size_t,int);void *heap_caps_malloc(size_t,int);void heap_caps_free(void*);
const char *esp_err_to_name(int);
