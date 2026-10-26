#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include "esp_stub.h"
/* Exercise the real OTA task with fake HTTP/flash edges. */
#include "../common/ota_update.c"

static void (*during_read)(void), (*during_end)(void);
static unsigned char response_bytes[1024], flashed_bytes[1024];
static size_t body_size, advertised_size, read_offset, max_chunk, written;
static int http_status, begin_calls, abort_calls, end_calls, boot_calls, restarted;
static bool type_header, version_header, id_header;
static const esp_partition_t partition = {"ota_0", 0x280000};
static esp_app_desc_t running = {.version = "2.7.0", .project_name = "hiphi_dial"};
/* The bytes the fake server delivers carry their own version, independent of what the version
 * endpoint advertised: the client has to check the binary, not just the metadata. */
static const char *image_version = "2.8.0";
static const char *advertised_version = "2.8.0";
static char last_url[256];
static const char *version_json = "{\"version\":\"2.8.0\",\"size\":1024}";
static int version_status = 200, task_creates;
static bool is_version_request(void) { return strstr(last_url, "/firmware/version") != NULL; }
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
static bool hold_worker, fail_worker_start;
static platform_task_fn_t held_worker;
static void *held_worker_arg;
static void (*during_start)(void);
void platform_task_finish(void) {}
int platform_task_start_internal_stack(const char *name, uint32_t stack, platform_task_fn_t fn, void *arg) {
    (void)stack;
    if (!strcmp(name,"ota_confirm")) { fn(arg); return 0; }
    ++task_creates;
    if (during_start) { void (*hook)(void) = during_start; during_start = NULL; hook(); }
    if (fail_worker_start) return -1;
    if (hold_worker) { held_worker = fn; held_worker_arg = arg; return 0; }
    fn(arg); return 0;
}
const char *esp_err_to_name(esp_err_t error) { return "test"; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg) { snprintf(last_url, sizeof(last_url), "%s", cfg->url); return (void *)1; }
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value) {
    if (!strcmp(key, "X-Device-Type")) { assert(!strcmp(value,device_slug)); type_header = true; }
    if (!strcmp(key, "X-Knob-Version")) { assert(!strcmp(value,running.version)); version_header = true; }
    if (!strcmp(key, "X-Knob-Id")) { assert(!strcmp(value,"aabbccddeeff")); id_header = true; }
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int len) { return ESP_OK; }
int esp_http_client_fetch_headers(esp_http_client_handle_t client) {
    return is_version_request() ? (int)strlen(version_json) : (int)advertised_size;
}
int esp_http_client_get_status_code(esp_http_client_handle_t client) {
    return is_version_request() ? version_status : http_status;
}
int esp_http_client_read(esp_http_client_handle_t client, char *out, int capacity) {
    if (during_read) { void (*hook)(void)=during_read; during_read=NULL; hook(); }
    if (is_version_request()) {
        int n = (int)strlen(version_json);
        if (n > capacity) n = capacity;
        memcpy(out, version_json, (size_t)n); return n;
    }
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
esp_err_t esp_ota_end(esp_ota_handle_t handle) { end_calls++; if (during_end) { void (*hook)(void)=during_end; during_end=NULL; hook(); } return ESP_OK; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p) { boot_calls++; return ESP_OK; }
const esp_app_desc_t *esp_app_get_description(void) { return &running; }
void esp_restart(void) { restarted++; }
static uint32_t endpoint_generation;
bool bridge_client_capture_request_endpoint(bridge_request_endpoint_t *out) {
    *out = (bridge_request_endpoint_t){.endpoint_generation=endpoint_generation};
    snprintf(out->base,sizeof(out->base),"http://bridge"); return true;
}
bool bridge_client_request_endpoint_current(const bridge_request_endpoint_t *out) {
    return out->endpoint_generation == endpoint_generation;
}
static void do_update_task(void *arg) {
    (void)arg;
    ota_job_t job={0}; bridge_client_capture_request_endpoint(&job.endpoint);
    ota_perform_update(&job);
}
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
    snprintf(app.version,sizeof(app.version),"%s",image_version);
    memcpy(response_bytes,&image,sizeof(image));
    memcpy(response_bytes+sizeof(image),&segment,sizeof(segment));
    memcpy(response_bytes+sizeof(image)+sizeof(segment),&app,sizeof(app));
    body_size=advertised_size=sizeof(response_bytes); max_chunk=4096; read_offset=written=0;
    http_status=200; begin_calls=abort_calls=end_calls=boot_calls=restarted=0;
    type_header=version_header=id_header=false; ota_init();
    snprintf(s_ota_info.available_version,sizeof(s_ota_info.available_version),"%s",advertised_version);
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
/* One request_install scenario: remote version served by the check, then the usual image. */
#if defined(OTA_TEST_PROJECT)
static void run_install(const char *remote, const char *from, bool expect_started) {
    version_json = remote; task_creates = 0; last_url[0] = 0;
    setup(running.project_name, TARGET_FLASH);
    task_creates = 0;
    assert(ota_request_install(from) == expect_started);
    assert(task_creates == (expect_started ? 1 : 0));
}

/* The downloaded bytes must be the release the bridge advertised, newer than what runs, on the same
 * channel. Metadata alone is not evidence of what was downloaded. */
static void test_downloaded_version_is_enforced(void) {
    set_running(OTA_TEST_PROJECT, OTA_TEST_SLUG, "2.7.0");
    struct { const char *image, *advertised; } bad[] = {
        {"1.0.0", "2.8.0"},          /* stale asset behind newer metadata */
        {"2.7.0", "2.8.0"},          /* equal to what runs */
        {"2.7.0", "2.7.0"},          /* advertised and delivered, but not newer */
        {"2.8.0-alpha.1", "2.8.0-alpha.1"}, /* newer number, wrong channel for a release build */
        {"2.9.0", "2.8.0"},          /* delivered newer than advertised: not what was selected */
        {"", "2.8.0"},               /* empty descriptor version */
    };
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); i++) {
        image_version = bad[i].image; advertised_version = bad[i].advertised;
        setup(OTA_TEST_PROJECT, TARGET_FLASH);
        run_rejected();
        assert(!strcmp(s_ota_info.error_msg, "Firmware version mismatch"));
    }
    /* A descriptor version with no terminator is never trusted. */
    image_version = "2.8.0"; advertised_version = "2.8.0";
    setup(OTA_TEST_PROJECT, TARGET_FLASH);
    memset(response_bytes + 48, 'x', 32);
    run_rejected();
    /* A leading 'v' on either side is only formatting. */
    image_version = "2.8.0"; advertised_version = "v2.8.0";
    setup(OTA_TEST_PROJECT, TARGET_FLASH);
    run_accepted();
    image_version = "2.8.0"; advertised_version = "2.8.0";
}

/* An install request that arrives while another OTA job runs is not lost: the job that finishes
 * starts it, even though the bridge's config hash was already accepted and will not be re-served. */
static void submit_during_start(void) {
    assert(ota_request_install_result("2.7.0") == OTA_REQUEST_ACCEPTED);
    assert(ota_request_install_result("2.7.0") == OTA_REQUEST_COALESCED);
    assert(s_have_pending && s_ota_busy);
}
static void test_busy_install_is_delivered_when_idle(void) {
    set_running(OTA_TEST_PROJECT, OTA_TEST_SLUG, "2.7.0");
    version_json="{\"version\":\"2.8.0\",\"size\":1024}";
    setup(OTA_TEST_PROJECT, TARGET_FLASH);
    hold_worker=true; task_creates=0; during_start=submit_during_start;
    ota_check_for_update(true);
    assert(task_creates==1 && s_ota_busy && s_have_pending);
    ota_info_t before, after; ota_get_info(&before); ota_init(); ota_get_info(&after);
    assert(!memcmp(&before,&after,sizeof(before)) && s_have_pending);
    hold_worker=false; held_worker(held_worker_arg);
    assert(task_creates==1 && restarted==1 && !s_ota_busy && !s_have_pending);
    /* Failure after a caller reserved pending work terminates both requests. */
    setup(OTA_TEST_PROJECT,TARGET_FLASH); fail_worker_start=true; during_start=submit_during_start;
    ota_check_for_update(true); fail_worker_start=false;
    ota_get_info(&after); assert(after.status==OTA_STATUS_ERROR && !s_ota_busy && !s_have_pending);
    /* Deferred request keeps its endpoint, including A->B->A invalidation. */
    setup(OTA_TEST_PROJECT,TARGET_FLASH); hold_worker=true;
    ota_check_for_update(true); assert(ota_request_install("2.7.0"));
    endpoint_generation+=2; hold_worker=false; held_worker(held_worker_arg);
    assert(begin_calls==0 && restarted==0 && !s_ota_busy);
    /* An immediate task completion cannot be overwritten by create returning. */
    setup(OTA_TEST_PROJECT,TARGET_FLASH); assert(ota_request_install("2.7.0"));
    assert(!s_ota_busy && restarted==1);
}
static void replace_endpoint(void) { endpoint_generation+=2; }
static void test_endpoint_replacement_during_effect(void) {
    setup(OTA_TEST_PROJECT,TARGET_FLASH);
    during_read=replace_endpoint;
    assert(ota_request_install("2.7.0"));
    assert(begin_calls==0 && boot_calls==0 && !s_ota_busy);
    setup(OTA_TEST_PROJECT,TARGET_FLASH);
    during_end=replace_endpoint;
    assert(ota_request_install("2.7.0"));
    assert(begin_calls==1 && end_calls==1 && boot_calls==0 && !s_ota_busy);
    setup(OTA_TEST_PROJECT,TARGET_FLASH); ota_check_for_update(true);
    ota_info_t snapshot; ota_get_info(&snapshot); assert(snapshot.status==OTA_STATUS_AVAILABLE);
    replace_endpoint(); ota_start_update();
    assert(begin_calls==0 && !s_ota_busy); /* stale metadata does not authorize a new endpoint */
}
static pthread_mutex_t callers_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t callers_ready=PTHREAD_COND_INITIALIZER;
static unsigned ready_count;
static bool release_callers;
static ota_request_result_t caller_results[16];
static void *concurrent_install(void *arg) {
    size_t index=(size_t)arg;
    pthread_mutex_lock(&callers_lock);
    ++ready_count; pthread_cond_broadcast(&callers_ready);
    while(!release_callers) pthread_cond_wait(&callers_ready,&callers_lock);
    pthread_mutex_unlock(&callers_lock);
    caller_results[index]=ota_request_install_result("2.7.0");
    return NULL;
}
static atomic_bool read_snapshots;
static void *snapshot_reader(void *arg) {
    (void)arg;
    do {
        ota_info_t info; assert(ota_get_info(&info));
        assert(!strcmp(info.current_version,"2.7.0"));
        assert(info.progress_percent>=0 && info.progress_percent<=100);
    } while(atomic_load(&read_snapshots));
    return NULL;
}
static void test_concurrent_admission_and_status(void) {
    setup(OTA_TEST_PROJECT,TARGET_FLASH); task_creates=0; hold_worker=true;
    ready_count=0; release_callers=false;
    pthread_t callers[16];
    for(size_t i=0;i<16;++i) assert(!pthread_create(&callers[i],NULL,concurrent_install,(void *)i));
    pthread_mutex_lock(&callers_lock);
    while(ready_count!=16) pthread_cond_wait(&callers_ready,&callers_lock);
    release_callers=true; pthread_cond_broadcast(&callers_ready);
    pthread_mutex_unlock(&callers_lock);
    unsigned accepted=0;
    for(size_t i=0;i<16;++i) {
        pthread_join(callers[i],NULL);
        assert(caller_results[i]==OTA_REQUEST_ACCEPTED || caller_results[i]==OTA_REQUEST_COALESCED);
        accepted+=caller_results[i]==OTA_REQUEST_ACCEPTED;
    }
    assert(accepted==1 && task_creates==1 && s_ota_busy && !s_have_pending);
    pthread_t reader; atomic_store(&read_snapshots,true);
    assert(!pthread_create(&reader,NULL,snapshot_reader,NULL));
    hold_worker=false; held_worker(held_worker_arg);
    atomic_store(&read_snapshots,false); pthread_join(reader,NULL);
    assert(begin_calls==1 && boot_calls==1 && !s_ota_busy);
}
static void test_install_trigger(void) {
    static char json[96];
    /* Stale request: the device already moved on, so nothing is checked or installed. */
    set_running(OTA_TEST_PROJECT, OTA_TEST_SLUG, "2.7.0");
    snprintf(json,sizeof(json),"{\"version\":\"2.8.0\",\"size\":1024}");
    run_install(json, "2.6.0", false);
    assert(last_url[0]==0 && begin_calls==0 && restarted==0);
    run_install(json, "2.7.0-beta.1", false);
    assert(last_url[0]==0 && restarted==0);
    /* Same version starts exactly one job that checks, then installs. */
    run_install(json, "2.7.0", true);
    assert(s_ota_info.status==OTA_STATUS_COMPLETE && begin_calls==1 && boot_calls==1 && restarted==1);
    assert(strstr(last_url,"/firmware/download"));
    assert(!strcmp(s_ota_info.available_version,"2.8.0"));
    /* An empty from_version carries no staleness claim. */
    run_install(json, "", true);
    assert(restarted==1 && boot_calls==1);
    run_install(json, NULL, true);
    assert(restarted==1);
    /* Never install a version that is not newer (equal, older, or a prerelease of it). */
    static const char *not_newer[]={"2.7.0","2.6.9","1.0.0","2.7.0-beta.9","2.7.0-rc.1"};
    for (size_t i=0;i<sizeof(not_newer)/sizeof(*not_newer);i++) {
        snprintf(json,sizeof(json),"{\"version\":\"%s\",\"size\":1024}",not_newer[i]);
        run_install(json, "2.7.0", true);
        assert(s_ota_info.status==OTA_STATUS_UP_TO_DATE);
        assert(begin_calls==0 && written==0 && boot_calls==0 && restarted==0);
        assert(strstr(last_url,"/firmware/version")); /* never reached the download */
    }
    /* A job already running ignores the request. */
    snprintf(json,sizeof(json),"{\"version\":\"2.8.0\",\"size\":1024}");
    version_json=json; setup(OTA_TEST_PROJECT,TARGET_FLASH); task_creates=0; last_url[0]=0;
    s_ota_busy=true; s_active_job.kind=OTA_JOB_CHECK;
    assert(ota_request_install("2.7.0") && task_creates==0 && last_url[0]==0 && restarted==0);
    s_ota_busy=false; s_have_pending=false;
    /* Failures leave ERROR, do not reboot, and free the slot for a later attempt. */
    version_status=500; run_install(json,"2.7.0",true); version_status=200;
    assert(s_ota_info.status==OTA_STATUS_ERROR && begin_calls==0 && restarted==0 && !s_ota_busy);
    version_json=json; setup("hiphi_nonexistent_sibling",TARGET_FLASH); task_creates=0;
    assert(ota_request_install("2.7.0") && task_creates==1);
    assert(s_ota_info.status==OTA_STATUS_ERROR && begin_calls==0 && boot_calls==0 && restarted==0);
    setup(OTA_TEST_PROJECT,TARGET_FLASH); http_status=404; task_creates=0;
    assert(ota_request_install("2.7.0") && s_ota_info.status==OTA_STATUS_ERROR && restarted==0);
    setup(OTA_TEST_PROJECT,TARGET_FLASH); task_creates=0;
    assert(ota_request_install("2.7.0") && s_ota_info.status==OTA_STATUS_COMPLETE && restarted==1);
}
#endif
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
    test_install_trigger();
    test_downloaded_version_is_enforced();
    test_busy_install_is_delivered_when_idle();
    test_endpoint_replacement_during_effect();
    test_concurrent_admission_and_status();
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
    image_version = advertised_version = "2.8.0-alpha.5";
    setup("hiphi_frame",TARGET_FLASH); do_update_task(NULL);
    assert(strstr(last_url,"/firmware/download") && strstr(last_url,"channel=alpha"));
    set_running("hiphi_frame","hiphi-frame","2.8.0");
    image_version = advertised_version = "2.8.1";
    setup("hiphi_frame",TARGET_FLASH); do_update_task(NULL);
    image_version = advertised_version = "2.8.0";
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
