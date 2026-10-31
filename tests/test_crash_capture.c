// Test real next-boot capture with injected storage and dump failures.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "crash_sdk.h"
#include "crash_report.h"
static crash_record_t stored;
static bool have_record,fail_commit,summary_fails,corrupt_read,partition_present=true;
static int erased;
static bool normal;
int nvs_open(const char*n,int m,nvs_handle_t*h){*h=1;return 0;}
int nvs_get_blob(nvs_handle_t h,const char*k,void*p,size_t*n){if(!have_record)return ESP_ERR_NVS_NOT_FOUND;*n=sizeof(stored);memcpy(p,&stored,*n);if(corrupt_read)((uint8_t*)p)[0]^=1;return 0;}
int nvs_set_blob(nvs_handle_t h,const char*k,const void*p,size_t n){memcpy(&stored,p,n);return 0;}
int nvs_commit(nvs_handle_t h){if(fail_commit)return ESP_FAIL;have_record=true;return 0;}
void nvs_close(nvs_handle_t h){}
int esp_reset_reason(void){return normal?1:ESP_RST_PANIC;}
const esp_app_desc_t *esp_app_get_description(void){static esp_app_desc_t a={"capture-build"};return &a;}
const char *platform_device_slug(void){return "hiphi-frame";}
const esp_partition_t *esp_partition_find_first(int a,int b,const char*c){static esp_partition_t p;return partition_present?&p:NULL;}
int esp_core_dump_image_check(void){return normal?ESP_FAIL:ESP_OK;}
int esp_core_dump_get_summary(esp_core_dump_summary_t*s){if(summary_fails)return ESP_FAIL;strcpy(s->exc_task,"ui_loop");strcpy(s->app_elf_sha256,"crashed-build");s->exc_pc=0x42001234;s->exc_bt_info.depth=1;s->exc_bt_info.bt[0]=0x42005678;return 0;}
int esp_core_dump_image_erase(void){erased++;return 0;}
int main(int argc,char**argv){
 assert(argc==2);fail_commit=!strcmp(argv[1],"commit");summary_fails=!strcmp(argv[1],"summary");corrupt_read=!strcmp(argv[1],"corrupt");partition_present=strcmp(argv[1],"old-layout")!=0;
 normal=!strcmp(argv[1],"normal");
 if(normal){have_record=true;strcpy(stored.capture_version,"old-build");crash_record_seal(&stored);}
 crash_report_capture_boot();
 if(normal){assert(erased==0&&!strcmp(stored.capture_version,"old-build"));assert(!strcmp(crash_report_capture_status(),"no_new_crash"));return 0;}
 if(fail_commit||summary_fails||corrupt_read){assert(erased==0);assert(strcmp(crash_report_capture_status(),"captured"));}
 else if(partition_present){assert(erased==1);crash_record_t r;assert(!strcmp(crash_report_read(&r),"available"));assert(r.has_dump&&!strcmp(r.crashed_elf_sha,"crashed-build"));}
 else {assert(erased==0);assert(have_record&&!stored.has_dump);}
 crash_report_capture_boot();assert(erased<=1);puts("crash_capture_test: ok");
}
