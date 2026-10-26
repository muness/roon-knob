#include <assert.h>
#include <string.h>
#include "esp_stub.h"
/* Exercise the real OTA task with fake HTTP/flash edges. */
#include "../common/ota_update.c"

static unsigned char response_bytes[1024], flashed_bytes[1024];
static size_t body_size, advertised_size, read_offset, max_chunk, written;
static int http_status, begin_calls, abort_calls, end_calls, boot_calls, restarted;
static bool type_header, version_header, id_header;
static const esp_partition_t partition = {"ota_0", 0x280000};
static esp_app_desc_t running = {.version = "2.7.0", .project_name = "hiphi_dial"};
static char last_url[256];
static int mark_valid_calls, mark_valid_result;
static const char *device_slug = "hiphi-dial";
#if defined(CONFIG_ESPTOOLPY_FLASHSIZE_8MB)
#define TARGET_FLASH ESP_IMAGE_FLASH_SIZE_8MB
#define OTHER_FLASH ESP_IMAGE_FLASH_SIZE_16MB
#else
#define TARGET_FLASH ESP_IMAGE_FLASH_SIZE_16MB
#define OTHER_FLASH ESP_IMAGE_FLASH_SIZE_8MB
#endif
#if defined(CONFIG_IDF_TARGET_ESP32)
#define TARGET_CHIP ESP_CHIP_ID_ESP32
#define OTHER_CHIP ESP_CHIP_ID_ESP32S3
#else
#define TARGET_CHIP ESP_CHIP_ID_ESP32S3
#define OTHER_CHIP ESP_CHIP_ID_ESP32
#endif

void vTaskDelete(TaskHandle_t task) {}
void vTaskDelay(int ticks) {}
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *handle) { task(arg); return 1; }
const char *esp_err_to_name(esp_err_t error) { return "test"; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg) { snprintf(last_url, sizeof(last_url), "%s", cfg->url); return (void *)1; }
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value) {
    if (!strcmp(key, "X-Device-Type")) { assert(!strcmp(value,device_slug)); type_header = true; }
    if (!strcmp(key, "X-Knob-Version")) { assert(!strcmp(value,running.version)); version_header = true; }
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
const char *platform_device_slug(void) { return device_slug; }
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) { mark_valid_calls++; return mark_valid_result; }

static void setup_chip(const char *project, unsigned flash_size, unsigned chip);
static void setup(const char *project, unsigned flash_size) {
    setup_chip(project, flash_size, TARGET_CHIP);
}
static void setup_chip(const char *project, unsigned flash_size, unsigned chip) {
    memset(response_bytes, 0xa5, sizeof(response_bytes));
    esp_image_header_t image = {.magic=ESP_IMAGE_HEADER_MAGIC, .segment_count=1,
        .chip_id=chip, .spi_size=flash_size};
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
static void run_accepted(void) {
    do_update_task(NULL);
    assert(s_ota_info.status==OTA_STATUS_COMPLETE && begin_calls==1 && end_calls==1 && boot_calls==1 && restarted==1);
    assert(written==body_size && !memcmp(response_bytes,flashed_bytes,body_size));
}
/* The running image names the only application it may be replaced by. */
static void set_running(const char *project, const char *slug, const char *version) {
    snprintf(running.project_name,sizeof(running.project_name),"%s",project);
    snprintf(running.version,sizeof(running.version),"%s",version);
    device_slug=slug;
}
int main(void) {
#if defined(OTA_TEST_ROLLBACK)
    /* Booting confirms the image exactly once, however many times init runs. */
    mark_valid_calls=0; ota_init(); ota_init(); ota_init();
    assert(mark_valid_calls==1);
#endif
#if defined(OTA_TEST_PROJECT)
    set_running(OTA_TEST_PROJECT, OTA_TEST_SLUG, "2.7.0");
    for (size_t chunk=1;chunk<=OTA_IMAGE_PREFIX_SIZE+1;chunk+=37) {
        setup(OTA_TEST_PROJECT,TARGET_FLASH); max_chunk=chunk; run_accepted();
        assert(type_header && version_header && id_header);
    }
    static const char *all[]={"hiphi_dial","hiphi_frame","hiphi_rlcd_42","hiphi_joy","hiphi_halo","hiphi_tough","hiphi_m5dial","hiphi_sticks3","hiphi_stopwatch","hiphi_stackchan","hiphi_knob_aux_park","roon_knob"};
    for(size_t i=0;i<sizeof(all)/sizeof(*all);i++) {
        if(!strcmp(all[i],OTA_TEST_PROJECT)) continue;
        if(!strcmp(all[i],"roon_knob") && !strcmp(OTA_TEST_PROJECT,"hiphi_dial")) continue;
        setup(all[i],TARGET_FLASH); run_rejected(); /* a sibling never installs */
    }
    setup(OTA_TEST_PROJECT,OTHER_FLASH); run_rejected();
    setup_chip(OTA_TEST_PROJECT,TARGET_FLASH,OTHER_CHIP); run_rejected();
    setup(OTA_TEST_PROJECT,TARGET_FLASH); response_bytes[0]=0; run_rejected();
    setup(OTA_TEST_PROJECT,TARGET_FLASH); response_bytes[32]=0; run_rejected(); /* descriptor magic */
    setup(OTA_TEST_PROJECT,TARGET_FLASH); memset(response_bytes+80,'x',32); run_rejected();
    setup(OTA_TEST_PROJECT,TARGET_FLASH); body_size=OTA_IMAGE_PREFIX_SIZE-1; max_chunk=1; run_rejected();
    setup(OTA_TEST_PROJECT,TARGET_FLASH); advertised_size=OTA_IMAGE_PREFIX_SIZE-1; run_rejected();
    setup(OTA_TEST_PROJECT,TARGET_FLASH); advertised_size=partition.size+1; run_rejected();
    setup(OTA_TEST_PROJECT,TARGET_FLASH); http_status=404; run_rejected();
    setup(OTA_TEST_PROJECT,TARGET_FLASH); body_size=OTA_IMAGE_PREFIX_SIZE+1; do_update_task(NULL);
    assert(begin_calls==1 && abort_calls==1 && end_calls==0 && boot_calls==0 && restarted==0);
    setup(OTA_TEST_PROJECT,TARGET_FLASH); advertised_size=OTA_IMAGE_PREFIX_SIZE+1; do_update_task(NULL);
    assert(begin_calls==1 && abort_calls==1 && written==OTA_IMAGE_PREFIX_SIZE && end_calls==0 && boot_calls==0);
#endif
#if defined(OTA_TEST_LEGACY_DIAL)
    /* Dial keeps accepting its historical project name. */
    set_running("hiphi_dial","hiphi-dial","2.7.0");
    setup("roon_knob",TARGET_FLASH); max_chunk=7; run_accepted();
    setup("roon_knob",OTHER_FLASH); run_rejected();
    /* A non-Dial target never accepts the Dial compatibility name. */
    set_running("hiphi_frame","hiphi-frame","2.7.0");
    setup("roon_knob",TARGET_FLASH); run_rejected();
#endif
#if defined(OTA_TEST_CHANNEL)
    /* A prerelease follows its own channel; the Stable feed would never offer it a build. */
    assert(!strcmp(ota_channel_for_version("2.8.0"),"stable"));
    assert(!strcmp(ota_channel_for_version("v2.8.0"),"stable"));
    assert(!strcmp(ota_channel_for_version("2.8.0-beta.3"),"beta"));
    assert(!strcmp(ota_channel_for_version("2.8.0-alpha.5"),"alpha"));
    assert(!strcmp(ota_channel_for_version("2.8.0-dev"),"alpha"));
    assert(!strcmp(ota_channel_for_version("2.8.0-rc.1"),"beta"));
    set_running("hiphi_frame","hiphi-frame","2.8.0-alpha.4");
    setup("hiphi_frame",TARGET_FLASH); do_update_task(NULL);
    assert(strstr(last_url,"/firmware/download") && strstr(last_url,"channel=alpha"));
    set_running("hiphi_frame","hiphi-frame","2.8.0");
    setup("hiphi_frame",TARGET_FLASH); do_update_task(NULL);
    assert(strstr(last_url,"channel=stable"));
    /* Numeric prerelease ordering: alpha.10 is newer than alpha.9 (strcmp says otherwise). */
    assert(ota_compare_versions("2.8.0-alpha.10","2.8.0-alpha.9")>0);
    assert(ota_compare_versions("2.8.0-alpha.9","2.8.0-alpha.10")<0);
    assert(ota_compare_versions("2.8.0-alpha.5","2.8.0-alpha.5")==0);
    assert(ota_compare_versions("2.8.0","2.8.0-beta.1")>0);
    assert(ota_compare_versions("2.8.0-beta.1","2.8.0-alpha.9")>0);
#endif
#if defined(OTA_TEST_ROLLBACK)
    /* A booted image confirms itself only through ota_confirm_boot(); a failing confirm is reported, not hidden. */
    mark_valid_calls=0; mark_valid_result=ESP_OK;
    assert(ota_confirm_boot()==true && mark_valid_calls==1);
    mark_valid_result=ESP_FAIL;
    assert(ota_confirm_boot()==false && mark_valid_calls==2);
#endif
    puts("OTA target/stream integration checks passed");
}
