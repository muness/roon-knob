#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "frame_sdk.h"
#include "../main/pmic_axp2101.c"
static uint8_t regs[256];
static bool stuck_scl, stuck_sda, missing, bad_readback;
static int writes, pulses, fail_reg = -1;
static int scl_mode, sda_mode;
static bool scl_gpio, sda_gpio;
int gpio_set_level(int p,int v){if(p==48&&v)pulses++;return 0;}
int gpio_get_level(int p){
 // ESP32 reads zero when GPIO input is disabled, even if an output pin is high.
 if(!(p==48?scl_gpio:sda_gpio)||(p==48?scl_mode:sda_mode)!=GPIO_MODE_INPUT_OUTPUT_OD)return 0;
 return p==48?!stuck_scl:!stuck_sda;
}
int gpio_config(const gpio_config_t*c){scl_mode=sda_mode=c->mode;scl_gpio=sda_gpio=true;return 0;}
int gpio_set_direction(int p,int m){if(p==48)scl_mode=m;else sda_mode=m;return 0;}int gpio_set_pull_mode(int p,int m){return 0;}int gpio_reset_pin(int p){return 0;}
void esp_rom_delay_us(int n){}void vTaskDelay(int n){}
int i2c_new_master_bus(const i2c_master_bus_config_t*c,void**b){*b=(void*)1;return 0;}
int i2c_master_bus_add_device(void*b,const i2c_device_config_t*c,void**d){*d=(void*)2;return 0;}
int i2c_master_transmit_receive(void*d,const uint8_t*r,size_t nr,uint8_t*v,size_t n,int t){if(missing)return -1;*v=regs[*r];if(bad_readback&&*r==0x94)*v=0;return 0;}
int i2c_master_transmit(void*d,const uint8_t*b,size_t n,int t){writes++;if(b[0]==fail_reg)return -1;regs[b[0]]=b[1];return 0;}
static void reset(void){memset(regs,0,sizeof(regs));missing=stuck_scl=stuck_sda=bad_readback=false;writes=pulses=0;fail_reg=-1;s_initialized=false;s_bus=NULL;s_dev=NULL;scl_mode=sda_mode=0;scl_gpio=sda_gpio=false;}
int main(void){
 reset();assert(pmic_init());assert((regs[0x90]&15)==15);assert(pulses>=9);
 reset();fail_reg=0x94;assert(!pmic_init());assert(pmic_power_source()==PMIC_POWER_SOURCE_UNKNOWN);
 reset();bad_readback=true;assert(!pmic_init());
 reset();stuck_scl=true;assert(!pmic_init());assert(writes==0);
 reset();stuck_sda=true;assert(!pmic_init());assert(writes==0);
 reset();missing=true;assert(!pmic_init());assert(writes==0);
 puts("pmic_recovery_test: ok");
}
