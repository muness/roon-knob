#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "frame_sdk.h"
#include "../main/eink_display.c"
static bool power_ok=true,busy_high=true,spi_fail,no_ack;
static int acknowledge_ticks;
static unsigned refresh_commands;
static uint64_t tick;
bool pmic_ensure_panel_power(void){return power_ok;}
int gpio_set_level(int p,int v){return 0;}
int gpio_get_level(int p){return busy_high;}
int gpio_config(const gpio_config_t*c){return 0;}
void vTaskDelay(int n){tick+=n;if(acknowledge_ticks>0&&!--acknowledge_ticks)busy_high=true;}
int spi_bus_initialize(int a,const spi_bus_config_t*b,int c){return 0;}
int spi_bus_add_device(int a,const spi_device_interface_config_t*b,void**c){*c=(void*)1;return 0;}
int spi_device_polling_transmit(void*a,spi_transaction_t*t){if(spi_fail)return -1;if(t->length==8&&*(uint8_t*)t->tx_buffer==0x12){refresh_commands++;if(!no_ack){busy_high=false;acknowledge_ticks=3;}}return 0;}
void *heap_caps_calloc(size_t n,size_t z,int cap){return calloc(n,z);}
void *heap_caps_malloc(size_t n,int cap){return malloc(n);}
void heap_caps_free(void*p){free(p);}const char *esp_err_to_name(int e){return "error";}
int main(void){
 assert(eink_display_init());
 frame_display_health_attempt("first",0);assert(eink_display_refresh());frame_display_health_complete(20000);
 frame_display_health_t h;frame_display_health_snapshot(&h);assert(h.completed==1&&!strcmp(h.displayed,"first"));
 unsigned commands=refresh_commands;
 frame_display_health_attempt("stuck",20001);busy_high=false;assert(!eink_display_refresh());frame_display_health_complete(60000);
 frame_display_health_snapshot(&h);assert(h.completed==1&&!strcmp(h.displayed,"first")&&strstr(h.error,"BUSY"));assert(commands==refresh_commands);
 busy_high=true;power_ok=false;frame_display_health_attempt("unpowered",60001);assert(!eink_display_refresh());frame_display_health_snapshot(&h);assert(!h.power_verified&&strstr(h.error,"rail"));assert(commands==refresh_commands);
 power_ok=true;spi_fail=true;frame_display_health_attempt("spi",60002);assert(!eink_display_refresh());frame_display_health_snapshot(&h);assert(strstr(h.error,"SPI"));assert(h.completed==1);
 spi_fail=false;no_ack=true;frame_display_health_attempt("no-ack",65000);assert(!eink_display_refresh());frame_display_health_complete(66000);frame_display_health_snapshot(&h);assert(h.completed==1&&strstr(h.error,"assert BUSY"));
 no_ack=false;frame_display_health_attempt("recovered",70000);assert(eink_display_refresh());frame_display_health_complete(90000);frame_display_health_snapshot(&h);assert(h.completed==2&&!strcmp(h.displayed,"recovered")&&!h.error[0]);
 free(s_fb);puts("display_failure_test: ok");
}
