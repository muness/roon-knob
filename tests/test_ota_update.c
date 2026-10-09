#include <assert.h>
#include <string.h>
#include "esp_stub.h"
/* Exercise the real OTA task with fake HTTP/flash edges. */
#include "../idf_app/main/ota_update.c"

static unsigned char response_bytes[1024], flashed_bytes[1024];
static size_t body_size, advertised_size, read_offset, max_chunk, written;
static int http_status, begin_calls, abort_calls, end_calls, boot_calls, restarted;
static bool type_header, version_header, id_header;
static const esp_partition_t partition = {"ota_0", 0x280000};
static esp_app_desc_t running = {.version = "2.7.0", .project_name = "hiphi_dial"};

void vTaskDelete(TaskHandle_t task) {}
void vTaskDelay(int ticks) {}
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *handle) { task(arg); return 1; }
const char *esp_err_to_name(esp_err_t error) { return "test"; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg) { return (void *)1; }
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value) {
    if (!strcmp(key, "X-Device-Type")) { assert(!strcmp(value,"hiphi-dial")); type_header = true; }
    if (!strcmp(key, "X-Knob-Version")) { assert(!strcmp(value,"2.7.0")); version_header = true; }
    if (!strcmp(key, "X-Knob-Id")) { assert(!strcmp(value,"aabbccddeeff")); id_header = true; }
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int len) { return ESP_OK; }
int esp_http_client_fetch_headers(esp_http_client_handle_t client) { return (int)advertised_size; }
int esp_http_client_get_status_code(esp_http_client_handle_t client) { return http_status; }
int esp_http_client_read(esp_http_client_handle_t client, char *out, int capacity) {
    size_t count = body_size - read_offset;
    if (count > (size_t)capacity) count = (size_t)capacity;
    if (count > max_chunk) count = max_chunk;
    memcpy(out, response_bytes + read_offset, count); read_offset += count; return (int)count;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) { return ESP_OK; }
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *p) { return &partition; }
esp_err_t esp_ota_begin(const esp_partition_t *p, size_t size, esp_ota_handle_t *handle) {
    assert(read_offset >= OTA_IMAGE_PREFIX_SIZE); assert(size == advertised_size);
    begin_calls++; *handle = 1; return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *bytes, size_t length) {
    assert(written + length <= sizeof(flashed_bytes));
    memcpy(flashed_bytes + written, bytes, length); written += length; return ESP_OK;
}
esp_err_t esp_ota_abort(esp_ota_handle_t handle) { abort_calls++; return ESP_OK; }
esp_err_t esp_ota_end(esp_ota_handle_t handle) { end_calls++; return ESP_OK; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p) { boot_calls++; return ESP_OK; }
const esp_app_desc_t *esp_app_get_description(void) { return &running; }
void esp_restart(void) { restarted++; }
bool bridge_client_get_request_base(char *out, size_t capacity) { snprintf(out,capacity,"http://bridge"); return true; }
void platform_http_get_knob_id(char *out, size_t capacity) { snprintf(out,capacity,"aabbccddeeff"); }
const char *platform_device_slug(void) { return "hiphi-dial"; }

static void setup(const char *project, unsigned flash_size) {
    memset(response_bytes, 0xa5, sizeof(response_bytes));
    esp_image_header_t image = {.magic=ESP_IMAGE_HEADER_MAGIC, .segment_count=1,
        .chip_id=ESP_CHIP_ID_ESP32S3, .spi_size=flash_size};
    esp_image_segment_header_t segment = {.data_len=sizeof(esp_app_desc_t)};
    esp_app_desc_t app = {.magic_word=ESP_APP_DESC_MAGIC_WORD};
    snprintf(app.project_name,sizeof(app.project_name),"%s",project);
    memcpy(response_bytes,&image,sizeof(image));
    memcpy(response_bytes+sizeof(image),&segment,sizeof(segment));
    memcpy(response_bytes+sizeof(image)+sizeof(segment),&app,sizeof(app));
    body_size=advertised_size=sizeof(response_bytes); max_chunk=4096; read_offset=written=0;
    http_status=200; begin_calls=abort_calls=end_calls=boot_calls=restarted=0;
    type_header=version_header=id_header=false; ota_init();
}
static void run_rejected(void) {
    do_update_task(NULL);
    assert(s_ota_info.status==OTA_STATUS_ERROR);
    assert(begin_calls==0 && written==0 && end_calls==0 && boot_calls==0 && restarted==0);
}
int main(void) {
    for (size_t chunk=1;chunk<=OTA_IMAGE_PREFIX_SIZE+1;chunk++) {
        setup("hiphi_dial",ESP_IMAGE_FLASH_SIZE_16MB); max_chunk=chunk; do_update_task(NULL);
        assert(s_ota_info.status==OTA_STATUS_COMPLETE && begin_calls==1 && end_calls==1 && boot_calls==1 && restarted==1);
        assert(written==body_size && !memcmp(response_bytes,flashed_bytes,body_size));
        assert(type_header && version_header && id_header);
    }
    setup("roon_knob",ESP_IMAGE_FLASH_SIZE_16MB); max_chunk=7; do_update_task(NULL); assert(restarted==1);
    const char *siblings[]={"hiphi_frame","hiphi_rlcd_42","hiphi_joy","hiphi_tough","hiphi_m5dial","hiphi_sticks3","hiphi_stopwatch","hiphi_stackchan","hiphi_knob_aux_park"};
    for(size_t i=0;i<sizeof(siblings)/sizeof(*siblings);i++) { setup(siblings[i],4); run_rejected(); }
    setup("hiphi_dial",ESP_IMAGE_FLASH_SIZE_8MB); run_rejected(); /* historical M5 collision */
    setup("roon_knob",ESP_IMAGE_FLASH_SIZE_8MB); run_rejected();
    setup("hiphi_dial",4); response_bytes[12]=0; run_rejected(); /* wrong chip */
    setup("hiphi_dial",4); response_bytes[0]=0; run_rejected();
    setup("hiphi_dial",4); response_bytes[32]=0; run_rejected(); /* descriptor magic */
    setup("hiphi_dial",4); memset(response_bytes+80,'x',32); run_rejected();
    setup("hiphi_dial",4); body_size=OTA_IMAGE_PREFIX_SIZE-1; max_chunk=1; run_rejected();
    setup("hiphi_dial",4); advertised_size=OTA_IMAGE_PREFIX_SIZE-1; run_rejected();
    setup("hiphi_dial",4); advertised_size=partition.size+1; run_rejected();
    setup("hiphi_dial",4); http_status=404; run_rejected();
    setup("hiphi_dial",4); body_size=OTA_IMAGE_PREFIX_SIZE+1; do_update_task(NULL);
    assert(begin_calls==1 && abort_calls==1 && end_calls==0 && boot_calls==0 && restarted==0);
    setup("hiphi_dial",4); advertised_size=OTA_IMAGE_PREFIX_SIZE+1; do_update_task(NULL);
    assert(begin_calls==1 && abort_calls==1 && written==OTA_IMAGE_PREFIX_SIZE && end_calls==0 && boot_calls==0);
    puts("OTA target/stream integration checks passed");
}
