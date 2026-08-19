/*
 * SCSerial.cpp
 * 飞特串行舵机硬件接口层程序
 * 日期: 2024.4.2
 * 作者:
 */

#include "SCSerial.h"

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "FT_SCSerial";

SCSerial::SCSerial()
{
	IOTimeOut = 10;
	uart_num = UART_NUM_MAX;
}

SCSerial::SCSerial(u8 End):SCS(End)
{
	IOTimeOut = 10;
	uart_num = UART_NUM_MAX;
}

SCSerial::SCSerial(u8 End, u8 Level):SCS(End, Level)
{
	IOTimeOut = 10;
	uart_num = UART_NUM_MAX;
}

bool SCSerial::begin(uart_port_t uart_num, int baud_rate, int tx_pin, int rx_pin, int buf_size)
{
	this->uart_num = uart_num;

	uart_config_t uart_config = {
		.baud_rate = baud_rate,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.rx_flow_ctrl_thresh = 0,
		.source_clk = UART_SCLK_DEFAULT,
	};

	esp_err_t ret = uart_driver_install(uart_num, buf_size, buf_size, 0, NULL, 0);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
		return false;
	}

	ret = uart_param_config(uart_num, &uart_config);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
		uart_driver_delete(uart_num);
		this->uart_num = UART_NUM_MAX;
		return false;
	}

	ret = uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
		uart_driver_delete(uart_num);
		this->uart_num = UART_NUM_MAX;
		return false;
	}

	return true;
}

void SCSerial::end()
{
	if (uart_num < UART_NUM_MAX) {
		uart_driver_delete(uart_num);
		uart_num = UART_NUM_MAX;
	}
}

int SCSerial::readSCS(unsigned char *nDat, int nLen, unsigned long TimeOut)
{
	if (uart_num >= UART_NUM_MAX) {
		return 0;
	}

	int size = 0;
	int64_t t_begin = esp_timer_get_time() / 1000;

	while (1) {
		size_t available = 0;
		uart_get_buffered_data_len(uart_num, &available);

		if (available > 0) {
			unsigned char byte;
			int len = uart_read_bytes(uart_num, &byte, 1, 0);
			if (len > 0) {
				if (nDat) {
					nDat[size] = byte;
				}
				size++;
			}
		}

		if (size >= nLen) {
			break;
		}

		if ((esp_timer_get_time() / 1000) - t_begin > static_cast<int64_t>(TimeOut)) {
			break;
		}

		vTaskDelay(pdMS_TO_TICKS(1));
	}

	return size;
}

int SCSerial::readSCS(unsigned char *nDat, int nLen)
{
	if (uart_num >= UART_NUM_MAX) {
		return 0;
	}

	int size = 0;
	int64_t t_begin = esp_timer_get_time() / 1000;

	while (1) {
		size_t available = 0;
		uart_get_buffered_data_len(uart_num, &available);

		if (available > 0) {
			unsigned char byte;
			int len = uart_read_bytes(uart_num, &byte, 1, 0);
			if (len > 0) {
				if (nDat) {
					nDat[size] = byte;
				}
				size++;
				t_begin = esp_timer_get_time() / 1000;
			}
		}

		if (size >= nLen) {
			break;
		}

		if ((esp_timer_get_time() / 1000) - t_begin > static_cast<int64_t>(IOTimeOut)) {
			break;
		}

		vTaskDelay(pdMS_TO_TICKS(1));
	}

	return size;
}

int SCSerial::writeSCS(unsigned char *nDat, int nLen)
{
	if (uart_num >= UART_NUM_MAX || nDat == NULL) {
		return 0;
	}

	return uart_write_bytes(uart_num, reinterpret_cast<const char *>(nDat), nLen);
}

int SCSerial::writeSCS(unsigned char bDat)
{
	if (uart_num >= UART_NUM_MAX) {
		return 0;
	}

	return uart_write_bytes(uart_num, reinterpret_cast<const char *>(&bDat), 1);
}

void SCSerial::rFlushSCS()
{
	if (uart_num >= UART_NUM_MAX) {
		return;
	}

	uart_flush_input(uart_num);
}

void SCSerial::wFlushSCS()
{
	if (uart_num >= UART_NUM_MAX) {
		return;
	}

	uart_wait_tx_done(uart_num, pdMS_TO_TICKS(100));
}
