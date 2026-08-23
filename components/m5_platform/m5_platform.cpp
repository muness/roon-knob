#include "m5_platform.h"
#include "m5_stackchan_choreography.h"
#include "m5_stackchan_voice.h"
#include <M5Unified.h>
#include <atomic>
/* Voice networking is deliberately separate from audio ownership: M5Unified
 * requires the StackChan microphone and speaker to take turns. */
#include "esp_websocket_client.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "model_path.h"
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
#include "kizz_wake_word.h"
#endif
#if CONFIG_M5_PLATFORM_EXPECT_DIAL
#include <M5Dial.h>
#elif CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
#include <M5StackChan.h>
#elif CONFIG_M5_PLATFORM_EXPECT_ATOMS3_JOYSTICK
#include <AtomJoyStick.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>
#include <nvs.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char *TAG = "m5_platform";
static m5_platform_board_t s_board = M5_PLATFORM_BOARD_UNKNOWN;
static bool s_started = false;
static bool s_joystick = false;
static bool s_joystick_ready = false;
static bool s_has_imu = false;
static int32_t s_encoder_remainder = 0;
static m5_platform_power_snapshot_t s_power_snapshot = {
    -1, M5_PLATFORM_POWER_SOURCE_UNKNOWN, false};
static int64_t s_power_snapshot_us = 0;
static constexpr int64_t POWER_SNAPSHOT_CACHE_US = 15000000;

#if CONFIG_M5_PLATFORM_EXPECT_ATOMS3_JOYSTICK
static AtomJoyStick s_atom_joystick;
#endif

namespace {
enum class StackChanMotionPhase {
    idle,
    attention_hold,
    attention_returning,
    first_pose,
    second_pose,
    third_pose,
    fourth_pose,
    returning,
};

struct StackChanMotionState {
    bool enabled = false;
    bool initialized = false;
    bool faulted = false;
    bool has_queued = false;
    m5_platform_stackchan_expression_t pending = M5_PLATFORM_STACKCHAN_CELEBRATE;
    m5_platform_stackchan_expression_t queued = M5_PLATFORM_STACKCHAN_CELEBRATE;
    StackChanMotionPhase phase = StackChanMotionPhase::idle;
    uint8_t dance_variant = 0;
    m5_platform_stackchan_face_cue_t face_cue =
        M5_PLATFORM_STACKCHAN_FACE_NEUTRAL;
    int64_t deadline = 0;
} s_stackchan_motion;

struct StackChanVoiceState {
    const m5_stackchan_voice_phrase_t *phrase = nullptr;
    uint8_t note = 0;
    int64_t deadline = 0;
    int64_t last_started[11] = {};
    bool enabled = true;
} s_stackchan_voice;

constexpr uint8_t STACKCHAN_VOICE_GAINS[] = {96, 144, 192};
esp_websocket_client_handle_t s_voice_ws = nullptr;
esp_websocket_client_handle_t s_enrollment_ws = nullptr;
SemaphoreHandle_t s_voice_audio_lock = nullptr;
SemaphoreHandle_t s_voice_text_lock = nullptr;
SemaphoreHandle_t s_enrollment_lock = nullptr;
bool s_voice_network_ready = false;
bool s_voice_transport_configured = false;
std::atomic_bool s_voice_listener_enabled{false};
std::atomic_bool s_voice_microphone_active{false};
std::atomic_bool s_voice_waiting_for_response{false};
std::atomic_int64_t s_voice_response_deadline_us{0};
std::atomic_bool s_voice_resume_after_sound{false};
std::atomic_bool s_voice_listening_visual{false};
char s_voice_transcript[161] = {};
char s_voice_response[161] = {};
std::atomic_bool s_voice_wake_pending{false};
std::atomic_bool s_voice_remote_endpoint_pending{false};
std::atomic_bool s_voice_diagnostics_enabled{true};
std::atomic_bool s_enrollment_active{false};
std::atomic_bool s_enrollment_detected{false};
std::atomic_int64_t s_enrollment_suppress_wake_until_us{0};
std::atomic_bool s_enrollment_sending{false};
constexpr size_t ENROLLMENT_MAX_SAMPLES = 16000 * 5;
constexpr int64_t VOICE_RESPONSE_TIMEOUT_US = 45000000;
int16_t *s_enrollment_buffer = nullptr;
size_t s_enrollment_samples = 0;
size_t s_enrollment_target_samples = 0;
char s_enrollment_capture_id[97] = {};
char s_enrollment_upload_url[257] = {};
char s_enrollment_event[513] = {};
size_t s_enrollment_event_length = 0;
std::atomic_uint32_t s_voice_audio_epoch{0};
std::atomic_int64_t s_command_end_silence_us{3000000};
std::atomic_int64_t s_command_max_utterance_us{12000000};
const esp_afe_sr_iface_t *s_voice_afe = nullptr;
esp_afe_sr_data_t *s_voice_afe_data = nullptr;
int64_t s_voice_last_audio_log = 0;
m5_platform_voice_zone_provider_t s_voice_zone_provider = nullptr;

bool voice_set_endpointing_ms(int end_silence_ms, int max_utterance_ms) {
    if (end_silence_ms < 300 || end_silence_ms > 5000 ||
        max_utterance_ms < 3000 || max_utterance_ms > 20000)
        return false;
    nvs_handle_t handle;
    if (nvs_open("kizz_voice", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_u32(handle, "end_sil_ms",
                                static_cast<uint32_t>(end_silence_ms));
    if (err == ESP_OK)
        err = nvs_set_u32(handle, "max_utt_ms",
                          static_cast<uint32_t>(max_utterance_ms));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return false;
    s_command_end_silence_us = static_cast<int64_t>(end_silence_ms) * 1000;
    s_command_max_utterance_us = static_cast<int64_t>(max_utterance_ms) * 1000;
    return true;
}

bool voice_set_diagnostics_enabled(bool enabled) {
    nvs_handle_t handle;
    if (nvs_open("kizz_voice", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(handle, "diagnostics", enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return false;
    s_voice_diagnostics_enabled = enabled;
    return true;
}

void voice_load_endpointing() {
    nvs_handle_t handle;
    if (nvs_open("kizz_voice", NVS_READONLY, &handle) != ESP_OK) return;
    uint32_t end_silence_ms = 0;
    if (nvs_get_u32(handle, "end_sil_ms", &end_silence_ms) == ESP_OK &&
        end_silence_ms >= 300 && end_silence_ms <= 5000)
        s_command_end_silence_us =
            static_cast<int64_t>(end_silence_ms) * 1000;
    uint32_t max_utterance_ms = 0;
    if (nvs_get_u32(handle, "max_utt_ms", &max_utterance_ms) == ESP_OK &&
        max_utterance_ms >= 3000 && max_utterance_ms <= 20000)
        s_command_max_utterance_us =
            static_cast<int64_t>(max_utterance_ms) * 1000;
    uint8_t diagnostics = 0;
    if (nvs_get_u8(handle, "diagnostics", &diagnostics) == ESP_OK)
        s_voice_diagnostics_enabled = diagnostics != 0;
    nvs_close(handle);
}

bool voice_take_microphone() {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    if (!s_voice_listener_enabled) return true;
    if (s_voice_microphone_active) {
        kizz_wake_word_resume();
        return true;
    }
    M5.Speaker.stop();
    M5.Speaker.end();
    auto mic_config = M5.Mic.config();
    /* Keep M5Unified's official CoreS3/StackChan pins, port, channel mode,
     * and callbacks. A 4x software magnification gives Kizz useful room-scale
     * speech level while staying within M5Unified's supported microphone API. */
    mic_config.magnification = 4;
    mic_config.sample_rate = 16000;
    M5.Mic.config(mic_config);
    if (!M5.Mic.begin()) {
        ESP_LOGE(TAG, "Kizz microphone handoff failed through M5Unified");
        M5.Speaker.begin();
        return false;
    }
    ESP_LOGI(TAG, "Kizz audio ownership: speaker -> microphone");
    s_voice_microphone_active = true;
    /* voice_fetch_task continuously drains the AFE while the speaker owns the
     * I2S path. Resetting the AFE here races that task inside Espressif's VAD
     * network and can assert during error recovery. Fresh microphone frames
     * refill the already-drained pipeline after this handoff. */
    kizz_wake_word_resume();
    s_voice_audio_epoch.fetch_add(1, std::memory_order_relaxed);
    return true;
#else
    return false;
#endif
}

bool voice_take_speaker() {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    if (!s_voice_listener_enabled) return M5.Speaker.isRunning();
    if (!s_voice_microphone_active) return M5.Speaker.isRunning();
    kizz_wake_word_pause();
    s_voice_microphone_active = false;
    while (M5.Mic.isRecording()) M5.delay(1);
    M5.Mic.end();
    if (!M5.Speaker.begin()) {
        ESP_LOGE(TAG, "Kizz speaker handoff failed through M5Unified");
        if (M5.Mic.begin()) {
            s_voice_microphone_active = true;
            kizz_wake_word_resume();
        }
        return false;
    }
    ESP_LOGI(TAG, "Kizz audio ownership: microphone -> speaker");
    return true;
#else
    return false;
#endif
}

bool voice_send_text(const char *message) {
    if (!s_voice_ws || !esp_websocket_client_is_connected(s_voice_ws)) return false;
    const int length = static_cast<int>(strlen(message));
    return esp_websocket_client_send_text(
        s_voice_ws, message, length, pdMS_TO_TICKS(5000)) == length;
}

bool voice_send_start() {
    char zone_id[64] = {};
    if (s_voice_zone_provider)
        (void)s_voice_zone_provider(zone_id, sizeof(zone_id));

    cJSON *root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddStringToObject(root, "type", "start");
    if (zone_id[0]) {
        cJSON *context = cJSON_AddObjectToObject(root, "context");
        if (context) cJSON_AddStringToObject(context, "zone_id", zone_id);
    }
    char *message = cJSON_PrintUnformatted(root);
    const bool sent = message && voice_send_text(message);
    ESP_LOGI(TAG, "Kizz voice turn started with current zone '%s'",
             zone_id[0] ? zone_id : "(unknown)");
    cJSON_free(message);
    cJSON_Delete(root);
    return sent;
}

bool voice_send_audio(const int16_t *pcm, size_t bytes) {
    if (!s_voice_ws || !esp_websocket_client_is_connected(s_voice_ws) || !bytes)
        return false;
    return esp_websocket_client_send_bin(
        s_voice_ws, reinterpret_cast<const char *>(pcm),
        static_cast<int>(bytes), pdMS_TO_TICKS(1000)) == static_cast<int>(bytes);
}

bool enrollment_send_text(const char *message) {
    if (!s_enrollment_ws || !esp_websocket_client_is_connected(s_enrollment_ws))
        return false;
    const int length = static_cast<int>(strlen(message));
    return esp_websocket_client_send_text(
        s_enrollment_ws, message, length, pdMS_TO_TICKS(1000)) == length;
}

bool enrollment_upload_audio_http(const char *url, const char *capture_id,
                                  bool detected, const int16_t *pcm,
                                  size_t bytes) {
    if (!url || !capture_id || !pcm || !bytes) return false;

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 20000;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;

    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_header(client, "Connection", "close");
    esp_http_client_set_header(client, "X-Device-ID",
                               CONFIG_M5_PLATFORM_DEVICE_ID);
    esp_http_client_set_header(client, "X-Detected",
                               detected ? "true" : "false");
    esp_http_client_set_header(client, "X-Capture-ID", capture_id);

    bool sent = esp_http_client_open(client, static_cast<int>(bytes)) == ESP_OK;
    size_t written = 0;
    while (sent && written < bytes) {
        const int result = esp_http_client_write(
            client, reinterpret_cast<const char *>(pcm) + written,
            static_cast<int>(bytes - written));
        if (result <= 0) {
            sent = false;
            break;
        }
        written += static_cast<size_t>(result);
    }
    const int64_t headers = sent ? esp_http_client_fetch_headers(client) : -1;
    const int status = sent ? esp_http_client_get_status_code(client) : 0;
    sent = sent && headers >= 0 && status >= 200 && status < 300;
    ESP_LOGI(TAG, "Enrollment HTTP upload %s status=%d bytes=%u/%u",
             capture_id, status, static_cast<unsigned>(written),
             static_cast<unsigned>(bytes));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return sent;
}

bool enrollment_valid_token(const char *value) {
    if (!value || !value[0] || strlen(value) > 96) return false;
    for (const char *cursor = value; *cursor; ++cursor) {
        if (!isalnum(static_cast<unsigned char>(*cursor)) &&
            *cursor != '-' && *cursor != '_' && *cursor != '.' && *cursor != ':')
            return false;
    }
    return true;
}

void enrollment_send_hello() {
    cJSON *root = cJSON_CreateObject();
    cJSON *audio = root ? cJSON_AddObjectToObject(root, "audio") : nullptr;
    cJSON *preprocessing = audio
        ? cJSON_AddObjectToObject(audio, "preprocessing") : nullptr;
    if (!root || !audio || !preprocessing) {
        cJSON_Delete(root);
        return;
    }
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddStringToObject(root, "device_id", CONFIG_M5_PLATFORM_DEVICE_ID);
    cJSON_AddStringToObject(root, "device_profile",
                            CONFIG_M5_PLATFORM_DEVICE_PROFILE);
    cJSON_AddStringToObject(root, "firmware_sha", HIPHI_FIRMWARE_GIT_SHA);
    cJSON_AddNumberToObject(audio, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio, "channels", 1);
    cJSON_AddStringToObject(audio, "sample_format", "s16le");
    cJSON_AddStringToObject(audio, "frontend", "m5unified_mic");
    cJSON_AddStringToObject(audio, "gain_profile", "room_scale_4x");
    cJSON_AddNumberToObject(preprocessing, "m5unified_magnification", 4);
    cJSON_AddStringToObject(preprocessing, "wake_frontend",
                            "tflite_micro_speech");
    char *message = cJSON_PrintUnformatted(root);
    if (!message || !enrollment_send_text(message))
        ESP_LOGW(TAG, "Device enrollment hello could not be sent");
    cJSON_free(message);
    cJSON_Delete(root);
}

void enrollment_send_error(const char *capture_id, const char *reason) {
    char message[256] = {};
    snprintf(message, sizeof(message),
             "{\"type\":\"training_error\",\"capture_id\":\"%s\",\"message\":\"%s\"}",
             capture_id ? capture_id : "", reason);
    (void)enrollment_send_text(message);
}

bool enrollment_start_capture(const char *capture_id, const char *upload_url,
                              int duration_ms) {
    if (!enrollment_valid_token(capture_id) || duration_ms < 500 ||
        duration_ms > 5000 || !upload_url ||
        strncmp(upload_url, "http://", 7) != 0 || strlen(upload_url) > 256 ||
        !s_enrollment_lock || !s_enrollment_buffer) return false;
    if (!s_voice_microphone_active || s_voice_waiting_for_response) {
        enrollment_send_error(capture_id, "microphone_unavailable");
        return false;
    }
    if (xSemaphoreTake(s_enrollment_lock, pdMS_TO_TICKS(100)) != pdTRUE)
        return false;
    if (s_enrollment_active || s_enrollment_sending) {
        xSemaphoreGive(s_enrollment_lock);
        enrollment_send_error(capture_id, "capture_busy");
        return false;
    }
    strlcpy(s_enrollment_capture_id, capture_id,
            sizeof(s_enrollment_capture_id));
    strlcpy(s_enrollment_upload_url, upload_url,
            sizeof(s_enrollment_upload_url));
    s_enrollment_samples = 0;
    s_enrollment_target_samples =
        static_cast<size_t>(duration_ms) * 16000 / 1000;
    s_enrollment_detected = false;
    s_voice_wake_pending = false;
    s_voice_audio_epoch.fetch_add(1, std::memory_order_relaxed);
    s_enrollment_active = true;
    xSemaphoreGive(s_enrollment_lock);
    ESP_LOGI(TAG, "Directed enrollment capture %s started for %d ms",
             capture_id, duration_ms);
    return true;
}

void enrollment_upload_task(void *) {
    vTaskDelay(pdMS_TO_TICKS(150));
    const size_t completed_samples = s_enrollment_samples;
    char capture_id[sizeof(s_enrollment_capture_id)] = {};
    strlcpy(capture_id, s_enrollment_capture_id, sizeof(capture_id));
    char upload_url[sizeof(s_enrollment_upload_url)] = {};
    strlcpy(upload_url, s_enrollment_upload_url, sizeof(upload_url));
    const bool detected = s_enrollment_detected;
    s_enrollment_suppress_wake_until_us = esp_timer_get_time() + 1000000;
    s_voice_wake_pending = false;

    const size_t completed_bytes = completed_samples * sizeof(int16_t);
    const bool sent = enrollment_upload_audio_http(
        upload_url, capture_id, detected, s_enrollment_buffer,
        completed_bytes);
    if (!sent) {
        ESP_LOGW(TAG, "Enrollment capture %s could not be sent", capture_id);
        enrollment_send_error(capture_id, "upload_failed");
    } else {
        ESP_LOGI(TAG, "Enrollment capture %s stored (provisional detected=%s)",
                 capture_id, detected ? "true" : "false");
    }

    if (xSemaphoreTake(s_enrollment_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_enrollment_sending = false;
        s_enrollment_samples = 0;
        s_enrollment_capture_id[0] = '\0';
        s_enrollment_upload_url[0] = '\0';
        xSemaphoreGive(s_enrollment_lock);
    }
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    // A provisional detection pauses the model even though enrollment
    // deliberately suppresses the production turn. Restore the independent
    // detector after every directed capture, including failed uploads.
    kizz_wake_word_resume();
#endif
    vTaskDelete(nullptr);
}

void enrollment_capture_audio(const int16_t *pcm, size_t samples) {
    if (!s_enrollment_active || !s_enrollment_lock || !samples) return;
    if (xSemaphoreTake(s_enrollment_lock, pdMS_TO_TICKS(20)) != pdTRUE) return;
    if (!s_enrollment_active) {
        xSemaphoreGive(s_enrollment_lock);
        return;
    }
    const size_t remaining = s_enrollment_target_samples - s_enrollment_samples;
    const size_t copied = std::min(samples, remaining);
    memcpy(s_enrollment_buffer + s_enrollment_samples, pcm,
           copied * sizeof(int16_t));
    s_enrollment_samples += copied;
    if (s_enrollment_samples < s_enrollment_target_samples) {
        xSemaphoreGive(s_enrollment_lock);
        return;
    }
    s_enrollment_active = false;
    s_enrollment_sending = true;
    xSemaphoreGive(s_enrollment_lock);
    if (xTaskCreatePinnedToCore(enrollment_upload_task, "kizz_enroll_tx", 4096,
                                nullptr, 4, nullptr, 0) != pdPASS) {
        ESP_LOGE(TAG, "Enrollment upload task could not start");
        if (xSemaphoreTake(s_enrollment_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_enrollment_sending = false;
            s_enrollment_samples = 0;
            s_enrollment_capture_id[0] = '\0';
            s_enrollment_upload_url[0] = '\0';
            xSemaphoreGive(s_enrollment_lock);
        }
    }
}

void voice_recover_listener() {
    s_voice_waiting_for_response = false;
    s_voice_response_deadline_us = 0;
    if (s_voice_audio_lock &&
        xSemaphoreTake(s_voice_audio_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        voice_take_microphone();
        xSemaphoreGive(s_voice_audio_lock);
    }
    m5_platform_voice_feedback("idle");
}

void voice_rearm_listener_preserve_face() {
    s_voice_waiting_for_response = false;
    s_voice_response_deadline_us = 0;
    s_voice_resume_after_sound = false;
    if (s_voice_audio_lock &&
        xSemaphoreTake(s_voice_audio_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        voice_take_microphone();
        xSemaphoreGive(s_voice_audio_lock);
    }
}

void voice_feed_task(void *) {
    const int chunk = s_voice_afe->get_feed_chunksize(s_voice_afe_data);
    ESP_LOGI(TAG, "Kizz official AFE input: %d samples at 16000 Hz", chunk);
    auto *pcm = static_cast<int16_t *>(heap_caps_malloc(
        static_cast<size_t>(chunk) * sizeof(int16_t), MALLOC_CAP_8BIT));
    if (!pcm) {
        ESP_LOGE(TAG, "Kizz AFE input allocation failed");
        vTaskDelete(nullptr);
    }
    uint32_t wake_dropped_since_log = 0;
    int64_t wake_drop_log_at = 0;
    for (;;) {
        bool captured = false;
        if (!s_voice_waiting_for_response && s_voice_audio_lock &&
            xSemaphoreTake(s_voice_audio_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            captured = s_voice_microphone_active &&
                M5.Mic.record(pcm, static_cast<size_t>(chunk), 16000, false);
            xSemaphoreGive(s_voice_audio_lock);
        }
        if (captured) {
            const int64_t now = esp_timer_get_time();
            if (now - s_voice_last_audio_log >= 2000000) {
                int peak = 0;
                for (int i = 0; i < chunk; ++i)
                    peak = std::max(peak, std::abs(static_cast<int>(pcm[i])));
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
                ESP_LOGI(TAG,
                         "Kizz official mic frame: peak=%d wake_state=%s transitions=%u",
                         peak, kizz_wake_word_runtime_state(),
                         static_cast<unsigned>(
                             kizz_wake_word_transition_count()));
#else
                ESP_LOGI(TAG, "Kizz official mic frame: peak=%d", peak);
#endif
                s_voice_last_audio_log = now;
            }
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
            const size_t wake_samples =
                kizz_wake_word_feed(pcm, static_cast<size_t>(chunk));
            if (wake_samples != static_cast<size_t>(chunk) &&
                strcmp(kizz_wake_word_runtime_state(), "paused") != 0) {
                wake_dropped_since_log +=
                    static_cast<uint32_t>(chunk - wake_samples);
                if (now - wake_drop_log_at >= 2000000) {
                    ESP_LOGW(TAG,
                             "Kizz wake input dropped %u samples in 2s "
                             "(state=%s)",
                             static_cast<unsigned>(wake_dropped_since_log),
                             kizz_wake_word_runtime_state());
                    wake_dropped_since_log = 0;
                    wake_drop_log_at = now;
                }
            }
#endif
            enrollment_capture_audio(pcm, static_cast<size_t>(chunk));
            s_voice_afe->feed(s_voice_afe_data, pcm);
        }
        else vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void voice_fetch_task(void *) {
    bool wake_detected = false;
    bool command_armed = false;
    bool utterance_open = false;
    int64_t wake_detected_at = 0;
    int64_t utterance_opened_at = 0;
    int64_t last_command_speech_at = 0;
    size_t command_audio_bytes = 0;
    uint32_t speech_frames = 0;
    uint32_t silence_frames = 0;
    uint32_t turn_id = 0;
    uint32_t audio_epoch = s_voice_audio_epoch;
    for (;;) {
        if (audio_epoch != s_voice_audio_epoch) {
            audio_epoch = s_voice_audio_epoch;
            wake_detected = false;
            command_armed = false;
            utterance_open = false;
            wake_detected_at = 0;
            utterance_opened_at = 0;
            last_command_speech_at = 0;
            command_audio_bytes = 0;
            speech_frames = 0;
            silence_frames = 0;
            s_voice_remote_endpoint_pending = false;
        }
        if (s_voice_waiting_for_response) {
            const int64_t deadline = s_voice_response_deadline_us.load();
            if (deadline > 0 && esp_timer_get_time() >= deadline) {
                ESP_LOGE(TAG,
                         "Kizz voice response timed out; recovering listener locally");
                s_voice_response_deadline_us = 0;
                if (s_voice_text_lock &&
                    xSemaphoreTake(s_voice_text_lock,
                                   pdMS_TO_TICKS(20)) == pdTRUE) {
                    snprintf(s_voice_response, sizeof(s_voice_response),
                             "Voice response timed out");
                    xSemaphoreGive(s_voice_text_lock);
                }
                m5_platform_voice_feedback("clarify");
            }
            // Keep draining the official AFE while the response is being
            // rendered. Pausing fetch() lets the ringbuffer fill, drops mic
            // frames, and leaves the wake engine paused after a turn.
            (void)s_voice_afe->fetch_with_delay(s_voice_afe_data,
                                                pdMS_TO_TICKS(100));
            continue;
        }
        afe_fetch_result_t *result =
            s_voice_afe->fetch_with_delay(s_voice_afe_data, pdMS_TO_TICKS(100));
        if (!result || result->ret_value == ESP_FAIL) continue;
        const int64_t now = esp_timer_get_time();
        // A streaming provider can decide that a turn ended while the device
        // is still receiving speech. Treat that signal as a hint, not an
        // unconditional commit: local VAD must observe a non-speech frame too.
        const bool remote_endpoint_hint = wake_detected && command_armed &&
            utterance_open && s_voice_remote_endpoint_pending.load();
        const bool remote_endpoint = remote_endpoint_hint &&
            result->vad_state != VAD_SPEECH &&
            s_voice_remote_endpoint_pending.exchange(false);
        if (s_voice_wake_pending.exchange(false)) {
            ESP_LOGI(TAG, "Kizz wake detected on-device: custom model volume=%.1f dBFS",
                     static_cast<double>(result->data_volume));
            wake_detected = true;
            command_armed = false;
            utterance_open = false;
            wake_detected_at = now;
            utterance_opened_at = 0;
            last_command_speech_at = 0;
            command_audio_bytes = 0;
            speech_frames = 0;
            silence_frames = 0;
            ++turn_id;
            s_voice_remote_endpoint_pending = false;
            m5_platform_voice_feedback("listening");
            if (!voice_send_start()) {
                ESP_LOGW(TAG, "Kizz voice turn could not start; returning to listening");
                wake_detected = false;
                wake_detected_at = 0;
                voice_recover_listener();
            }
        } else if (wake_detected && !command_armed) {
            /* WakeNet fires while the wake phrase is still in VAD speech.
             * A natural pause arms the command immediately. If speech is
             * continuous, a short grace period plus VAD cache preserves the
             * beginning of the command without committing the wake-word tail. */
            if (result->vad_state != VAD_SPEECH) {
                command_armed = true;
                ESP_LOGI(TAG, "Kizz command armed after wake-word pause");
            } else if (now - wake_detected_at >= 250000) {
                command_armed = true;
                utterance_open = true;
                utterance_opened_at = now;
                last_command_speech_at = now;
                ++speech_frames;
                ESP_LOGI(TAG, "Kizz command armed during continuous speech");
                if (result->vad_cache_size > 0)
                    if (voice_send_audio(result->vad_cache,
                                         static_cast<size_t>(result->vad_cache_size)))
                        command_audio_bytes += static_cast<size_t>(result->vad_cache_size);
                if (voice_send_audio(result->data,
                                     static_cast<size_t>(result->data_size)))
                    command_audio_bytes += static_cast<size_t>(result->data_size);
            }
        } else if (wake_detected && command_armed && utterance_open &&
                   (remote_endpoint ||
                    (result->vad_state != VAD_SPEECH &&
                     now - last_command_speech_at >=
                         s_command_end_silence_us.load()) ||
                    now - utterance_opened_at >=
                        s_command_max_utterance_us.load())) {
            const bool maximum_reached =
                now - utterance_opened_at >= s_command_max_utterance_us.load();
            const char *reason = remote_endpoint ? "deepgram_flux" :
                (maximum_reached ? "max_duration" : "vad_silence");
            const int64_t trailing_silence_us = std::max<int64_t>(
                0, now - last_command_speech_at);
            ESP_LOGI(TAG,
                     "Kizz utterance committed: reason=%s wake_to_commit=%lldms "
                     "command=%lldms silence=%lldms bytes=%u vad=%u/%u",
                     reason,
                     static_cast<long long>((now - wake_detected_at) / 1000),
                     static_cast<long long>((now - utterance_opened_at) / 1000),
                     static_cast<long long>(trailing_silence_us / 1000),
                     static_cast<unsigned>(command_audio_bytes),
                     static_cast<unsigned>(speech_frames),
                     static_cast<unsigned>(silence_frames));
            char telemetry[384] = {};
            snprintf(telemetry, sizeof(telemetry),
                     "{\"type\":\"voice_telemetry\",\"event\":\"endpoint\","
                     "\"turn_id\":%u,\"reason\":\"%s\","
                     "\"wake_to_commit_ms\":%lld,\"command_ms\":%lld,"
                     "\"trailing_silence_ms\":%lld,\"audio_bytes\":%u,"
                     "\"speech_frames\":%u,\"silence_frames\":%u,"
                     "\"end_silence_ms\":%lld,\"max_utterance_ms\":%lld}",
                     static_cast<unsigned>(turn_id), reason,
                     static_cast<long long>((now - wake_detected_at) / 1000),
                     static_cast<long long>((now - utterance_opened_at) / 1000),
                     static_cast<long long>(trailing_silence_us / 1000),
                     static_cast<unsigned>(command_audio_bytes),
                     static_cast<unsigned>(speech_frames),
                     static_cast<unsigned>(silence_frames),
                     static_cast<long long>(s_command_end_silence_us.load() / 1000),
                     static_cast<long long>(s_command_max_utterance_us.load() / 1000));
            (void)enrollment_send_text(telemetry);
            wake_detected = false;
            command_armed = false;
            utterance_open = false;
            utterance_opened_at = 0;
            last_command_speech_at = 0;
            s_voice_waiting_for_response = true;
            s_voice_response_deadline_us =
                esp_timer_get_time() + VOICE_RESPONSE_TIMEOUT_US;
            if (s_voice_audio_lock &&
                xSemaphoreTake(s_voice_audio_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
                voice_take_speaker();
                xSemaphoreGive(s_voice_audio_lock);
            }
            if (!voice_send_text("{\"type\":\"commit\"}")) {
                ESP_LOGW(TAG, "Kizz voice commit failed; returning to listening");
                voice_recover_listener();
            }
        } else if (wake_detected && command_armed &&
                   result->vad_state == VAD_SPEECH) {
            if (!utterance_open) {
                utterance_open = true;
                utterance_opened_at = now;
                if (result->vad_cache_size > 0)
                    if (voice_send_audio(result->vad_cache,
                                         static_cast<size_t>(result->vad_cache_size)))
                        command_audio_bytes += static_cast<size_t>(result->vad_cache_size);
            }
            last_command_speech_at = now;
            ++speech_frames;
            if (voice_send_audio(result->data,
                                 static_cast<size_t>(result->data_size)))
                command_audio_bytes += static_cast<size_t>(result->data_size);
        } else if (wake_detected && command_armed && utterance_open) {
            ++silence_frames;
            if (voice_send_audio(result->data,
                                 static_cast<size_t>(result->data_size)))
                command_audio_bytes += static_cast<size_t>(result->data_size);
        } else if (wake_detected && !utterance_open &&
                   now - wake_detected_at >= 6000000) {
            ESP_LOGI(TAG, "Kizz listening timed out without command speech");
            wake_detected = false;
            command_armed = false;
            s_voice_waiting_for_response = true;
            s_voice_response_deadline_us =
                esp_timer_get_time() + VOICE_RESPONSE_TIMEOUT_US;
            if (s_voice_audio_lock &&
                xSemaphoreTake(s_voice_audio_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
                voice_take_speaker();
                xSemaphoreGive(s_voice_audio_lock);
            }
            if (!voice_send_text("{\"type\":\"commit\"}")) {
                ESP_LOGW(TAG, "Kizz empty voice commit failed; returning to listening");
                voice_recover_listener();
            }
        }
    }
}

void voice_ws_event(void *, esp_event_base_t, int32_t event_id, void *event_data) {
    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "Kizz local voice gateway connected");
        s_voice_waiting_for_response = false;
        s_voice_response_deadline_us = 0;
        m5_platform_voice_feedback("idle");
        return;
    }
    if (event_id == WEBSOCKET_EVENT_DISCONNECTED || event_id == WEBSOCKET_EVENT_ERROR) {
        ESP_LOGW(TAG, "Kizz local voice gateway unavailable; native controls remain safe");
        if (s_voice_waiting_for_response) voice_recover_listener();
        return;
    }
    if (event_id != WEBSOCKET_EVENT_DATA) return;
    const auto *event = static_cast<esp_websocket_event_data_t *>(event_data);
    if (!event || (event->op_code != 0x1 && event->op_code != 0x0) ||
        !event->data_ptr ||
        event->data_len <= 0 || event->data_len > 512) return;
    char message[513] = {};
    memcpy(message, event->data_ptr, static_cast<size_t>(event->data_len));
    cJSON *root = cJSON_Parse(message);
    if (!root) return;
    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type) && type->valuestring &&
        strcmp(type->valuestring, "transcript") == 0) {
        cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");
        if (cJSON_IsString(text) && text->valuestring) {
            if (s_voice_text_lock &&
                xSemaphoreTake(s_voice_text_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
                snprintf(s_voice_transcript, sizeof(s_voice_transcript), "%s",
                         text->valuestring);
                xSemaphoreGive(s_voice_text_lock);
            }
            ESP_LOGI(TAG, "Kizz transcript: %s", s_voice_transcript);
        }
    }
    if (cJSON_IsString(type) && type->valuestring &&
        strcmp(type->valuestring, "endpoint") == 0) {
        s_voice_remote_endpoint_pending = true;
    }
    cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (cJSON_IsString(state) && state->valuestring) {
        cJSON *heard = cJSON_GetObjectItemCaseSensitive(root, "heard");
        cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
        if (cJSON_IsString(heard) && heard->valuestring) {
            if (s_voice_text_lock &&
                xSemaphoreTake(s_voice_text_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
                snprintf(s_voice_transcript, sizeof(s_voice_transcript), "%s",
                         heard->valuestring);
                xSemaphoreGive(s_voice_text_lock);
            }
        }
        if (cJSON_IsString(message) && message->valuestring) {
            if (s_voice_text_lock &&
                xSemaphoreTake(s_voice_text_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
                snprintf(s_voice_response, sizeof(s_voice_response), "%s",
                         message->valuestring);
                xSemaphoreGive(s_voice_text_lock);
            }
            ESP_LOGI(TAG, "Kizz response: %s", s_voice_response);
        }
        m5_platform_voice_feedback(state->valuestring);
    }
    cJSON_Delete(root);
}

void enrollment_ws_event(void *, esp_event_base_t, int32_t event_id,
                         void *event_data) {
    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "Independent enrollment service connected");
        enrollment_send_hello();
        return;
    }
    if (event_id == WEBSOCKET_EVENT_DISCONNECTED ||
        event_id == WEBSOCKET_EVENT_ERROR) {
        ESP_LOGW(TAG, "Independent enrollment service unavailable; production voice unchanged");
        return;
    }
    if (event_id != WEBSOCKET_EVENT_DATA) return;
    const auto *event = static_cast<esp_websocket_event_data_t *>(event_data);
    if (!event || event->op_code != 0x1 || !event->data_ptr ||
        event->data_len <= 0 || event->payload_len <= 0 ||
        event->payload_len > static_cast<int>(sizeof(s_enrollment_event) - 1) ||
        event->payload_offset < 0 ||
        event->payload_offset + event->data_len > event->payload_len) return;
    if (event->payload_offset == 0) {
        s_enrollment_event_length = static_cast<size_t>(event->payload_len);
        memset(s_enrollment_event, 0, sizeof(s_enrollment_event));
    }
    if (s_enrollment_event_length != static_cast<size_t>(event->payload_len))
        return;
    memcpy(s_enrollment_event + event->payload_offset, event->data_ptr,
           static_cast<size_t>(event->data_len));
    if (event->payload_offset + event->data_len != event->payload_len) return;

    cJSON *root = cJSON_ParseWithLength(
        s_enrollment_event, s_enrollment_event_length);
    s_enrollment_event_length = 0;
    if (!root) return;
    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type) && type->valuestring &&
        strcmp(type->valuestring, "training_capture") == 0) {
        cJSON *capture_id = cJSON_GetObjectItemCaseSensitive(root, "capture_id");
        cJSON *duration_ms = cJSON_GetObjectItemCaseSensitive(root, "duration_ms");
        cJSON *upload_url = cJSON_GetObjectItemCaseSensitive(root, "upload_url");
        if (!cJSON_IsString(capture_id) || !capture_id->valuestring ||
            !cJSON_IsString(upload_url) || !upload_url->valuestring ||
            !cJSON_IsNumber(duration_ms) ||
            !enrollment_start_capture(capture_id->valuestring,
                                      upload_url->valuestring,
                                      duration_ms->valueint))
            ESP_LOGW(TAG, "Rejected malformed or unavailable enrollment capture");
    } else if (cJSON_IsString(type) && type->valuestring &&
               strcmp(type->valuestring, "wake_config") == 0) {
        cJSON *cutoff = cJSON_GetObjectItemCaseSensitive(
            root, "probability_cutoff");
        cJSON *window = cJSON_GetObjectItemCaseSensitive(
            root, "sliding_window");
        cJSON *end_silence = cJSON_GetObjectItemCaseSensitive(
            root, "end_silence_ms");
        cJSON *max_utterance = cJSON_GetObjectItemCaseSensitive(
            root, "max_utterance_ms");
        cJSON *diagnostics = cJSON_GetObjectItemCaseSensitive(
            root, "diagnostics_enabled");
        if (!cJSON_IsNumber(cutoff) || !cJSON_IsNumber(window) ||
            !cJSON_IsNumber(end_silence) || !cJSON_IsNumber(max_utterance) ||
            !kizz_wake_word_configure(
                static_cast<float>(cutoff->valuedouble),
                static_cast<size_t>(window->valueint)) ||
            !voice_set_endpointing_ms(end_silence->valueint,
                                      max_utterance->valueint)) {
            ESP_LOGW(TAG, "Rejected invalid wake calibration");
            enrollment_send_text(
                "{\"type\":\"wake_config_error\",\"reason\":\"invalid_config\"}");
        } else {
            if (cJSON_IsBool(diagnostics) &&
                !voice_set_diagnostics_enabled(cJSON_IsTrue(diagnostics))) {
                ESP_LOGW(TAG, "Could not persist voice diagnostics setting");
            }
            char response[240];
            snprintf(response, sizeof(response),
                     "{\"type\":\"wake_config_applied\","
                     "\"probability_cutoff\":%.3f,\"sliding_window\":%d,"
                     "\"end_silence_ms\":%d,\"max_utterance_ms\":%d,"
                     "\"diagnostics_enabled\":%s}",
                     cutoff->valuedouble, window->valueint,
                     end_silence->valueint, max_utterance->valueint,
                     s_voice_diagnostics_enabled ? "true" : "false");
            enrollment_send_text(response);
        }
    }
    cJSON_Delete(root);
}

void start_voice_transport() {
    if (s_voice_ws || s_enrollment_ws || !s_voice_network_ready) return;
    constexpr const char *voice_uri = CONFIG_M5_PLATFORM_STACKCHAN_VOICE_WS_URI;
    constexpr const char *enrollment_uri = CONFIG_M5_PLATFORM_ENROLLMENT_WS_URI;
    s_voice_transport_configured = voice_uri[0] != '\0';
    if (!s_voice_transport_configured && !enrollment_uri[0]) {
        ESP_LOGI(TAG, "Neither production voice nor enrollment is configured");
        return;
    }
    s_voice_audio_lock = xSemaphoreCreateMutex();
    s_voice_text_lock = xSemaphoreCreateMutex();
    if (!s_voice_audio_lock || !s_voice_text_lock) {
        ESP_LOGE(TAG, "Kizz voice audio lock allocation failed");
        return;
    }
    s_voice_listener_enabled = true;
    voice_load_endpointing();
    const bool locked =
        xSemaphoreTake(s_voice_audio_lock, pdMS_TO_TICKS(100)) == pdTRUE;
    if (!locked || !voice_take_microphone()) {
        if (locked) xSemaphoreGive(s_voice_audio_lock);
        s_voice_listener_enabled = false;
        return;
    }
    xSemaphoreGive(s_voice_audio_lock);

    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models) {
        ESP_LOGE(TAG, "Kizz ESP-SR model partition unavailable");
        s_voice_listener_enabled = false;
        return;
    }
    afe_config_t *afe_config =
        afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (!afe_config) {
        ESP_LOGE(TAG, "Kizz ESP-SR AFE configuration failed");
        s_voice_listener_enabled = false;
        return;
    }
    afe_config->wakenet_init = false;
    afe_config->vad_init = true;
    afe_config->vad_min_speech_ms = 128;
    afe_config->vad_min_noise_ms = 700;
    afe_config->vad_delay_ms = 256;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    s_voice_afe = esp_afe_handle_from_config(afe_config);
    s_voice_afe_data = s_voice_afe
        ? s_voice_afe->create_from_config(afe_config) : nullptr;
    afe_config_free(afe_config);
    if (!s_voice_afe || !s_voice_afe_data) {
        ESP_LOGE(TAG, "Kizz ESP-SR AFE initialization failed");
        s_voice_listener_enabled = false;
        return;
    }
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    if (!kizz_wake_word_start([]() {
            if (s_enrollment_active || s_enrollment_sending)
                s_enrollment_detected = true;
            else if (esp_timer_get_time() >=
                         s_enrollment_suppress_wake_until_us.load() &&
                     s_voice_transport_configured)
                s_voice_wake_pending = true;
        })) {
        ESP_LOGE(TAG, "Kizz custom wake model initialization failed");
        s_voice_listener_enabled = false;
        return;
    }
#endif

    if (s_voice_transport_configured) {
        esp_websocket_client_config_t voice_cfg = {};
        voice_cfg.uri = voice_uri;
        voice_cfg.buffer_size = 2048;
        voice_cfg.network_timeout_ms = 10000;
        voice_cfg.reconnect_timeout_ms = 1000;
        voice_cfg.enable_close_reconnect = true;
        s_voice_ws = esp_websocket_client_init(&voice_cfg);
        if (!s_voice_ws) {
            ESP_LOGE(TAG, "Kizz voice transport allocation failed");
            return;
        }
        ESP_LOGI(TAG, "Starting Kizz local voice gateway at %s", voice_cfg.uri);
        esp_websocket_register_events(s_voice_ws, WEBSOCKET_EVENT_ANY,
                                      voice_ws_event, nullptr);
        esp_websocket_client_start(s_voice_ws);
    }
    if (enrollment_uri[0]) {
        s_enrollment_lock = xSemaphoreCreateMutex();
        s_enrollment_buffer = static_cast<int16_t *>(heap_caps_calloc(
            ENROLLMENT_MAX_SAMPLES, sizeof(int16_t), MALLOC_CAP_SPIRAM));
        if (!s_enrollment_lock || !s_enrollment_buffer) {
            ESP_LOGE(TAG, "Enrollment buffer allocation failed; production voice remains enabled");
        } else {
            esp_websocket_client_config_t enrollment_cfg = {};
            enrollment_cfg.uri = enrollment_uri;
            enrollment_cfg.buffer_size = 2048;
            enrollment_cfg.network_timeout_ms = 10000;
            enrollment_cfg.reconnect_timeout_ms = 1000;
            enrollment_cfg.enable_close_reconnect = true;
            s_enrollment_ws = esp_websocket_client_init(&enrollment_cfg);
            if (!s_enrollment_ws) {
                ESP_LOGE(TAG, "Enrollment transport allocation failed");
            } else {
                ESP_LOGI(TAG, "Starting independent enrollment endpoint at %s",
                         enrollment_cfg.uri);
                esp_websocket_register_events(
                    s_enrollment_ws, WEBSOCKET_EVENT_ANY,
                    enrollment_ws_event, nullptr);
                esp_websocket_client_start(s_enrollment_ws);
            }
        }
    }
    xTaskCreatePinnedToCore(voice_feed_task, "kizz_afe_feed", 6144,
                            nullptr, 5, nullptr, 0);
    if (s_voice_transport_configured)
        xTaskCreatePinnedToCore(voice_fetch_task, "kizz_afe_fetch", 6144,
                                nullptr, 5, nullptr, 1);
}

void stackchan_voice_note(uint8_t index) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    const auto &note = s_stackchan_voice.phrase->notes[index];
    if (!M5.Speaker.tone(note.frequency_hz, note.duration_ms, 0, true)) {
        ESP_LOGE(TAG, "Kizz speaker rejected note %u for %s",
                 static_cast<unsigned>(index), s_stackchan_voice.phrase->name);
        s_stackchan_voice.phrase = nullptr;
        return;
    }
    s_stackchan_voice.deadline = esp_timer_get_time() +
        static_cast<int64_t>(note.duration_ms + note.gap_ms) * 1000;
#else
    (void)index;
#endif
}

void stackchan_motion_fail(const char *reason) {
    ESP_LOGE(TAG, "Kizz body language disabled: %s", reason);
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    M5StackChan.Motion.setTorqueEnabled(false);
    M5StackChan.setServoPowerEnabled(false);
#endif
    s_stackchan_motion.faulted = true;
    s_stackchan_motion.enabled = false;
    s_stackchan_motion.phase = StackChanMotionPhase::idle;
    s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_NEUTRAL;
}

void stackchan_quiesce_for_sleep() {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    if (!s_started || s_board != M5_PLATFORM_BOARD_STACKCHAN) return;
    s_stackchan_voice.phrase = nullptr;
    M5.Speaker.stop();
    M5.Speaker.end();
    M5StackChan.Motion.setTorqueEnabled(false);
    M5StackChan.setServoPowerEnabled(false);
    s_stackchan_motion.has_queued = false;
    s_stackchan_motion.phase = StackChanMotionPhase::idle;
    s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_RESTING;
#endif
}

void stackchan_resume_from_sleep() {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    if (!s_started || s_board != M5_PLATFORM_BOARD_STACKCHAN) return;
    if (s_stackchan_motion.enabled && !s_stackchan_motion.faulted) {
        M5StackChan.setServoPowerEnabled(true);
    }
    if (s_stackchan_voice.enabled && M5.Speaker.isEnabled() &&
        !M5.Speaker.isRunning() && !M5.Speaker.begin()) {
        ESP_LOGW(TAG, "Kizz speaker did not resume after display sleep");
    }
    s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_NEUTRAL;
#endif
}

void stackchan_pose(int yaw, int pitch, int speed) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    M5StackChan.Motion.setTorqueEnabled(true);
    M5StackChan.Motion.move(yaw, pitch, speed);
#else
    (void)yaw;
    (void)pitch;
    (void)speed;
#endif
}

void stackchan_begin_listening_pose() {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    if (!s_stackchan_motion.enabled || s_stackchan_motion.faulted ||
        s_stackchan_motion.phase != StackChanMotionPhase::idle) return;
    // Breazeal, "Proto-conversations with an anthropomorphic robot" (RO-MAN
    // 2000), section 3: Kismet leaned toward the speaker while holding eye
    // contact. Kizz has two axes, so a centered forward lean carries it.
    stackchan_pose(0, 160, 220);
    s_stackchan_motion.phase = StackChanMotionPhase::attention_hold;
    ESP_LOGI(TAG, "Kizz listening posture: forward and centered");
#endif
}

void stackchan_end_listening_pose() {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    if (s_stackchan_motion.phase != StackChanMotionPhase::attention_hold)
        return;
    // Kismet leaned back and shifted gaze before taking its turn. The face
    // supplies the gaze cue; the official BSP returns the body to neutral.
    M5StackChan.Motion.goHome(220);
    s_stackchan_motion.phase = StackChanMotionPhase::attention_returning;
    s_stackchan_motion.deadline = esp_timer_get_time() + 650000;
#endif
}

void stackchan_dance_pose(size_t index) {
    const auto &pose =
        M5_STACKCHAN_DANCES[s_stackchan_motion.dance_variant][index];
    s_stackchan_motion.face_cue = pose.face;
    stackchan_pose(pose.yaw_angle, pose.pitch_angle, pose.speed);
}

int64_t stackchan_dance_deadline(size_t index) {
    return esp_timer_get_time() +
           static_cast<int64_t>(
               M5_STACKCHAN_DANCES[s_stackchan_motion.dance_variant][index]
                   .hold_ms) *
               1000;
}

bool expected_board(m5::board_t board) {
#if CONFIG_M5_PLATFORM_EXPECT_ATOMS3_JOYSTICK
    return board == m5::board_t::board_M5AtomS3;
#elif CONFIG_M5_PLATFORM_EXPECT_TOUGH
    return board == m5::board_t::board_M5Tough;
#elif CONFIG_M5_PLATFORM_EXPECT_DIAL
    return board == m5::board_t::board_M5Dial;
#elif CONFIG_M5_PLATFORM_EXPECT_STICKS3
    return board == m5::board_t::board_M5StickS3;
#elif CONFIG_M5_PLATFORM_EXPECT_STOPWATCH
    return board == m5::board_t::board_M5StopWatch;
#elif CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    return board == m5::board_t::board_M5StackChan;
#else
    return true;
#endif
}

uint8_t joystick_adc12_to_u8(uint16_t value) {
    value = std::min<uint16_t>(value, 4095);
    return static_cast<uint8_t>((static_cast<uint32_t>(value) * 255 + 2047) /
                                4095);
}
}

extern "C" bool m5_platform_begin(void) {
    auto cfg = M5.config();
    cfg.clear_display = true;
    cfg.output_power = true;
    cfg.pmic_button = true;
    cfg.internal_imu = true;
    cfg.internal_rtc = false;
    cfg.internal_spk = false;
    cfg.internal_mic = false;
    cfg.led_brightness = 0;

#if CONFIG_M5_PLATFORM_EXPECT_DIAL || \
    CONFIG_M5_PLATFORM_EXPECT_STACKCHAN || \
    CONFIG_M5_PLATFORM_EXPECT_ATOMS3_JOYSTICK
    initArduino();
#endif
#if CONFIG_M5_PLATFORM_EXPECT_DIAL
    M5Dial.begin(cfg, true, false);
#elif CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    M5StackChan.begin();
#else
    M5.begin(cfg);
#endif
    const auto board = M5.getBoard();
    if (board != m5::board_t::board_M5Tough &&
        board != m5::board_t::board_M5AtomS3 &&
        board != m5::board_t::board_M5Dial &&
        board != m5::board_t::board_M5StickS3 &&
        board != m5::board_t::board_M5StopWatch &&
        board != m5::board_t::board_M5StackChan) {
        ESP_LOGE(TAG, "Unsupported M5 board detected: %d",
                 static_cast<int>(M5.getBoard()));
        return false;
    }
    if (!expected_board(board)) {
        ESP_LOGE(TAG, "Firmware/board mismatch: detected %d",
                 static_cast<int>(board));
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.setTextDatum(middle_center);
        M5.Display.drawString("WRONG FIRMWARE", M5.Display.width() / 2,
                              M5.Display.height() / 2);
        return false;
    }
    if (M5.Display.width() == 0 || M5.Display.height() == 0) {
        ESP_LOGE(TAG, "M5 display is not enabled");
        return false;
    }

    s_joystick = board == m5::board_t::board_M5AtomS3;
    M5.Display.setRotation(
        board == m5::board_t::board_M5Tough ||
        board == m5::board_t::board_M5StackChan ? 1 : 0);
    /* Bridge artwork is RGB565 in network byte order. M5GFX's image path
     * needs byte swapping for the Tough panel; primitive colors are
     * unaffected by this setting. */
    M5.Display.setSwapBytes(true);
    M5.Display.setBrightness(180);
    s_board = board == m5::board_t::board_M5AtomS3 ? M5_PLATFORM_BOARD_ATOMS3_JOYSTICK :
              board == m5::board_t::board_M5Tough ? M5_PLATFORM_BOARD_TOUGH :
              board == m5::board_t::board_M5Dial ? M5_PLATFORM_BOARD_DIAL :
              board == m5::board_t::board_M5StickS3 ? M5_PLATFORM_BOARD_STICKS3 :
              board == m5::board_t::board_M5StopWatch ? M5_PLATFORM_BOARD_STOPWATCH :
              M5_PLATFORM_BOARD_STACKCHAN;
    s_has_imu = M5.Imu.isEnabled();
    s_started = true;
    if (s_joystick) {
#if CONFIG_M5_PLATFORM_EXPECT_ATOMS3_JOYSTICK
        s_joystick_ready = s_atom_joystick.begin();
        if (!s_joystick_ready) {
            ESP_LOGW(TAG, "Official Atom JoyStick library did not find the base");
        } else {
            ESP_LOGI(TAG, "Qualified AtomS3 Joystick via M5Stack library: "
                          "%ux%u firmware=%u",
                     M5.Display.width(), M5.Display.height(),
                     s_atom_joystick.getFirmwareVersion());
        }
#else
        ESP_LOGE(TAG, "AtomS3 Joystick firmware lacks its official board library");
        return false;
#endif
    } else if (s_board == M5_PLATFORM_BOARD_STACKCHAN) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
        const auto angles = M5StackChan.Motion.getCurrentAngles();
        if (angles.x < -1280 || angles.x > 1280 ||
            angles.y < 0 || angles.y > 900) {
            stackchan_motion_fail("official BSP returned invalid angles");
        } else {
            M5StackChan.Motion.setAutoAngleSyncEnabled(false);
            M5StackChan.Motion.setAutoTorqueReleaseEnabled(true);
            M5StackChan.Motion.setTorqueEnabled(false);
            s_stackchan_motion.initialized = true;
            if (M5.Speaker.isEnabled()) {
                /* M5Unified's own speaker example uses master=64 and channel
                 * 255. The enclosed AW88298 is read from listening distance,
                 * so give the short phrases modest headroom above that while
                 * retaining the official amplifier configuration. */
                M5.Speaker.setVolume(
                    STACKCHAN_VOICE_GAINS[M5_PLATFORM_STACKCHAN_VOLUME_LOW]);
                M5.Speaker.setAllChannelVolume(255);
                if (M5.Speaker.begin()) {
                    ESP_LOGI(TAG,
                             "Kizz proto-voice ready via M5Unified: "
                             "running=%d master=%u channel=%u",
                             M5.Speaker.isRunning(), M5.Speaker.getVolume(),
                             M5.Speaker.getChannelVolume(0));
                } else {
                    ESP_LOGW(TAG, "Kizz M5Unified speaker did not start");
                }
            } else {
                ESP_LOGW(TAG, "Kizz speaker unavailable to M5Unified");
            }
            ESP_LOGI(TAG, "Qualified Kizz on M5StackChan via M5Stack BSP: yaw=%d pitch=%d",
                     angles.x, angles.y);
        }
#endif
    } else {
        ESP_LOGI(TAG, "Qualified %s: %ux%u touch=%s imu=%s",
                 m5_platform_board_name(), M5.Display.width(),
                 M5.Display.height(), M5.Touch.isEnabled() ? "yes" : "no",
                 s_has_imu ? "yes" : "no");
    }
    return true;
}

extern "C" void m5_platform_update(void) {
    if (s_started) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
        if (s_board == M5_PLATFORM_BOARD_STACKCHAN) {
            M5StackChan.update();
            m5_platform_stackchan_sound_process();
            return;
        }
#elif CONFIG_M5_PLATFORM_EXPECT_DIAL
        if (s_board == M5_PLATFORM_BOARD_DIAL) {
            M5Dial.update();
            return;
        }
#endif
        M5.update();
    }
}

extern "C" m5_platform_board_t m5_platform_board(void) {
    return s_board;
}

extern "C" const char *m5_platform_board_name(void) {
    return s_board == M5_PLATFORM_BOARD_TOUGH ? "M5Stack Tough" :
           s_board == M5_PLATFORM_BOARD_ATOMS3_JOYSTICK ? "AtomS3 Joystick" :
           s_board == M5_PLATFORM_BOARD_DIAL ? "M5 Dial" :
           s_board == M5_PLATFORM_BOARD_STICKS3 ? "M5StickS3" :
           s_board == M5_PLATFORM_BOARD_STOPWATCH ? "M5Stack StopWatch" :
           s_board == M5_PLATFORM_BOARD_STACKCHAN ? "Kizz" :
           "unknown";
}

extern "C" uint16_t m5_platform_display_width(void) {
    return s_started ? static_cast<uint16_t>(M5.Display.width()) : 0;
}

extern "C" uint16_t m5_platform_display_height(void) {
    return s_started ? static_cast<uint16_t>(M5.Display.height()) : 0;
}

extern "C" void m5_platform_voice_set_zone_provider(
    m5_platform_voice_zone_provider_t provider) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    s_voice_zone_provider = provider;
#else
    (void)provider;
#endif
}

extern "C" void m5_platform_voice_network_ready(void) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    s_voice_network_ready = true;
    start_voice_transport();
#endif
}

extern "C" void m5_platform_voice_feedback(const char *state) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    if (!state || !s_started || s_board != M5_PLATFORM_BOARD_STACKCHAN) return;
    if (strcmp(state, "listening") != 0) stackchan_end_listening_pose();
    if (strcmp(state, "listening") == 0) {
        m5_platform_voice_clear_conversation();
        /* Keep recording intact. The attentive face is the immediate cue;
         * the chirp comes after the sentence when audio is safely handed off. */
        s_voice_listening_visual = true;
        s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_ATTENTIVE;
        stackchan_begin_listening_pose();
    } else if (strcmp(state, "thinking") == 0) {
        s_voice_listening_visual = false;
        s_voice_resume_after_sound = false;
        s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_CURIOUS;
        m5_platform_stackchan_sound_trigger(M5_PLATFORM_STACKCHAN_SOUND_MORE);
    } else if (strcmp(state, "success") == 0) {
        s_voice_listening_visual = false;
        s_voice_resume_after_sound = true;
        s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_PROUD;
        if (!m5_platform_stackchan_sound_trigger(
                M5_PLATFORM_STACKCHAN_SOUND_NEW_TRACK))
            voice_rearm_listener_preserve_face();
        m5_platform_stackchan_expression_trigger(M5_PLATFORM_STACKCHAN_DANCE);
    } else if (strcmp(state, "clarify") == 0) {
        s_voice_listening_visual = false;
        s_voice_resume_after_sound = true;
        s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_WORRIED;
        if (!m5_platform_stackchan_sound_trigger(M5_PLATFORM_STACKCHAN_SOUND_LOST))
            voice_rearm_listener_preserve_face();
    } else {
        s_voice_listening_visual = false;
        s_voice_waiting_for_response = false;
        s_voice_response_deadline_us = 0;
        s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_NEUTRAL;
        if (s_stackchan_voice.phrase) {
            // An idle transport event can arrive while a connection or result
            // chirp still owns the speaker. Reopening the shared M5Unified I2S
            // path here lets later notes steal the pins back from the mic.
            // Keep the wake runtime paused until sound_process completes the
            // phrase and performs one ordered speaker -> microphone handoff.
            s_voice_resume_after_sound = true;
            ESP_LOGI(TAG, "Kizz listener rearm deferred until sound completes");
        } else if (s_voice_listener_enabled && s_voice_audio_lock &&
                   xSemaphoreTake(s_voice_audio_lock,
                                  pdMS_TO_TICKS(100)) == pdTRUE) {
            voice_take_microphone();
            xSemaphoreGive(s_voice_audio_lock);
        }
    }
#else
    (void)state;
#endif
}

extern "C" bool m5_platform_voice_is_listening(void) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    return s_voice_listening_visual;
#else
    return false;
#endif
}

extern "C" const char *m5_platform_voice_state(void) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    if (s_enrollment_active) return "CAPTURING";
    if (s_enrollment_sending) return "UPLOADING";
    if (s_voice_listening_visual) return "LISTENING";
    if (s_stackchan_voice.phrase) return "SPEAKING";
    if (s_voice_waiting_for_response) return "THINKING";
    if (s_voice_resume_after_sound) return "RECOVERING";
    const char *wake_state = kizz_wake_word_runtime_state();
    if (strcmp(wake_state, "armed") == 0) return "ARMED";
    if (strcmp(wake_state, "starting") == 0) return "STARTING";
    if (strcmp(wake_state, "stopping") == 0) return "STOPPING";
    if (strcmp(wake_state, "paused") == 0) return "PAUSED";
    if (strcmp(wake_state, "faulted") == 0) return "FAULT";
    return "UNKNOWN";
#else
    return "DISABLED";
#endif
}

extern "C" void m5_platform_voice_copy_transcript(char *out, size_t len) {
    if (!out || !len) return;
    out[0] = '\0';
    if (s_voice_text_lock &&
        xSemaphoreTake(s_voice_text_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        snprintf(out, len, "%s", s_voice_transcript);
        xSemaphoreGive(s_voice_text_lock);
    }
}

extern "C" void m5_platform_voice_copy_response(char *out, size_t len) {
    if (!out || !len) return;
    out[0] = '\0';
    if (s_voice_text_lock &&
        xSemaphoreTake(s_voice_text_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        snprintf(out, len, "%s", s_voice_response);
        xSemaphoreGive(s_voice_text_lock);
    }
}

extern "C" void m5_platform_voice_clear_conversation(void) {
    if (s_voice_text_lock &&
        xSemaphoreTake(s_voice_text_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_voice_transcript[0] = '\0';
        s_voice_response[0] = '\0';
        xSemaphoreGive(s_voice_text_lock);
    }
}

extern "C" float m5_platform_voice_wake_probability(void) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    return kizz_wake_word_probability();
#else
    return 0.0f;
#endif
}

extern "C" float m5_platform_voice_wake_cutoff(void) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    float cutoff = 0.0f;
    kizz_wake_word_get_config(&cutoff, nullptr);
    return cutoff;
#else
    return 0.0f;
#endif
}

extern "C" bool m5_platform_voice_diagnostics_enabled(void) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    return s_voice_diagnostics_enabled;
#else
    return false;
#endif
}

extern "C" bool m5_platform_touch_event(m5_platform_touch_event_t *out) {
    if (!out || !s_started || s_joystick) {
        return false;
    }
    const auto &detail = M5.Touch.getDetail(0);
    out->x = detail.x;
    out->y = detail.y;
    out->delta_x = static_cast<int16_t>(detail.deltaX());
    out->delta_y = static_cast<int16_t>(detail.deltaY());
    if (detail.wasPressed()) {
        out->state = M5_PLATFORM_TOUCH_PRESSED;
    } else if (detail.isDragging() || detail.isFlicking() ||
               (detail.isPressed() && (out->delta_x != 0 || out->delta_y != 0))) {
        /* M5Unified's documented drag state is deliberately conservative: it
         * starts after the hold threshold. Preserve raw pressed samples with
         * movement so a normal quick swipe remains scrollable. */
        out->state = M5_PLATFORM_TOUCH_DRAGGING;
    } else if (detail.wasHold()) {
        out->state = M5_PLATFORM_TOUCH_HELD;
    } else if (detail.wasClicked()) {
        out->state = M5_PLATFORM_TOUCH_CLICKED;
    } else if (detail.wasReleased()) {
        out->state = M5_PLATFORM_TOUCH_RELEASED;
    } else {
        out->state = M5_PLATFORM_TOUCH_NONE;
    }
    return out->state != M5_PLATFORM_TOUCH_NONE;
}

extern "C" bool m5_platform_joystick_state(m5_platform_joystick_state_t *out) {
    if (!out || !s_started || !s_joystick || !s_joystick_ready) return false;
#if CONFIG_M5_PLATFORM_EXPECT_ATOMS3_JOYSTICK
    out->left_x = joystick_adc12_to_u8(
        s_atom_joystick.getJoy1ADCValueX(_12bit));
    out->left_y = joystick_adc12_to_u8(
        s_atom_joystick.getJoy1ADCValueY(_12bit));
    out->right_x = joystick_adc12_to_u8(
        s_atom_joystick.getJoy2ADCValueX(_12bit));
    out->right_y = joystick_adc12_to_u8(
        s_atom_joystick.getJoy2ADCValueY(_12bit));
    /* M5Stack's public button values are active-low. */
    out->top_left_pressed = s_atom_joystick.getButtonValue(BUTTON_1) == 0;
    out->top_right_pressed = s_atom_joystick.getButtonValue(BUTTON_2) == 0;
    out->left_stick_pressed = s_atom_joystick.getButtonValue(BUTTON_A) == 0;
    out->right_stick_pressed = s_atom_joystick.getButtonValue(BUTTON_B) == 0;
#else
    return false;
#endif
    return true;
}

extern "C" bool m5_platform_surface_button_event(
    m5_platform_surface_button_event_t *out) {
    if (!out || !s_started) return false;
    out->pressed = M5.BtnA.isPressed();
    out->clicked = M5.BtnA.wasClicked();
    out->single_clicked = M5.BtnA.wasSingleClicked();
    out->double_clicked = M5.BtnA.wasDoubleClicked();
    out->held = M5.BtnA.wasHold();
    out->secondary_pressed = M5.BtnB.isPressed();
    out->secondary_clicked = M5.BtnB.wasClicked();
    out->secondary_held = M5.BtnB.wasHold();
    return out->pressed || out->clicked || out->held ||
           out->single_clicked || out->double_clicked ||
           out->secondary_pressed || out->secondary_clicked ||
           out->secondary_held;
}

extern "C" bool m5_platform_encoder_delta(int32_t *out_delta) {
    if (!out_delta || !s_started || s_board != M5_PLATFORM_BOARD_DIAL) return false;
#if CONFIG_M5_PLATFORM_EXPECT_DIAL
    /* M5Dial's official encoder reports quadrature edges. The controller's
     * human-facing unit is one physical detent, so retain partial edges. */
    s_encoder_remainder += M5Dial.Encoder.readAndReset();
    const int32_t detents = s_encoder_remainder / 4;
    s_encoder_remainder -= detents * 4;
    *out_delta = detents;
    return true;
#else
    return false;
#endif
}

extern "C" bool m5_platform_gyro(float *out_x, float *out_y, float *out_z) {
    if (!out_x || !out_y || !out_z || !s_started || !s_has_imu) return false;
    return M5.Imu.getGyro(out_x, out_y, out_z);
}

extern "C" bool m5_platform_accel(float *out_x, float *out_y, float *out_z) {
    if (!out_x || !out_y || !out_z || !s_started || !s_has_imu) return false;
    return M5.Imu.getAccel(out_x, out_y, out_z);
}

extern "C" bool m5_platform_haptic(uint8_t strength) {
    if (!s_started || s_board != M5_PLATFORM_BOARD_STOPWATCH) return false;
    M5.Power.setVibration(strength);
    return true;
}

extern "C" bool m5_platform_battery_is_charging(void) {
    m5_platform_power_snapshot_t snapshot = {};
    return m5_platform_power_snapshot(&snapshot) &&
           snapshot.external_power_policy;
}

extern "C" int m5_platform_battery_level(void) {
    m5_platform_power_snapshot_t snapshot = {};
    return m5_platform_power_snapshot(&snapshot) ? snapshot.battery_level : -1;
}

extern "C" bool m5_platform_power_snapshot(
    m5_platform_power_snapshot_t *out) {
    if (!out || !s_started) return false;
    const int64_t now = esp_timer_get_time();
    if (s_power_snapshot_us == 0 ||
        now - s_power_snapshot_us >= POWER_SNAPSHOT_CACHE_US) {
        m5_platform_power_snapshot_t next = {
            -1, M5_PLATFORM_POWER_SOURCE_UNKNOWN, false};
#if CONFIG_M5_PLATFORM_EXPECT_ATOMS3_JOYSTICK
        /* The base's STM32 owns both battery ADC channels. There is no VBUS
         * status register, so report that uncertainty while choosing the
         * portable/battery policy. Use the higher valid pack reading because
         * either JST battery socket may be populated. */
        const float battery1 = s_atom_joystick.getBattery1Voltage();
        const float battery2 = s_atom_joystick.getBattery2Voltage();
        const float volts = std::max(battery1, battery2);
        if (volts >= 2.5f && volts <= 4.5f) {
            next.battery_level = std::clamp(
                static_cast<int>((volts - 3.20f) * 100.0f / 1.00f), 0, 100);
        }
#else
        next.battery_level = static_cast<int>(M5.Power.getBatteryLevel());
        const int16_t vbus_mv = M5.Power.getVBUSVoltage();
        const bool external = vbus_mv >= 4000 ||
            M5.Power.isCharging() == m5::Power_Class::is_charging;
        /* Dial v1.1's VBUS/charging status is not exposed through its
         * qualified BSP. The other supported PMIC boards do expose VBUS. */
        if (s_board == M5_PLATFORM_BOARD_DIAL) {
            next.source = M5_PLATFORM_POWER_SOURCE_UNKNOWN;
            next.external_power_policy = false;
        } else {
            next.source = external ? M5_PLATFORM_POWER_SOURCE_EXTERNAL
                                   : M5_PLATFORM_POWER_SOURCE_BATTERY;
            next.external_power_policy = external;
        }
#endif
        s_power_snapshot = next;
        s_power_snapshot_us = now;
    }
    *out = s_power_snapshot;
    return true;
}

extern "C" bool m5_platform_stackchan_expression_enable(bool enabled) {
    ESP_LOGI(TAG, "Kizz expression enable=%d started=%d board=%d",
             enabled, s_started, static_cast<int>(s_board));
    if (!s_started || s_board != M5_PLATFORM_BOARD_STACKCHAN) return false;
    if (!enabled) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
        M5StackChan.Motion.setTorqueEnabled(false);
        M5StackChan.setServoPowerEnabled(false);
#endif
        s_stackchan_motion.enabled = false;
        s_stackchan_motion.has_queued = false;
        s_stackchan_motion.phase = StackChanMotionPhase::idle;
        s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_NEUTRAL;
        return true;
    }
    if (!s_stackchan_motion.initialized || s_stackchan_motion.faulted) {
        ESP_LOGE(TAG, "Kizz's official M5StackChan BSP was not qualified");
        return false;
    }
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    M5StackChan.setServoPowerEnabled(true);
#endif
    s_stackchan_motion.enabled = true;
    return true;
}

extern "C" bool m5_platform_stackchan_expression_trigger(
    m5_platform_stackchan_expression_t expression) {
    if (!s_stackchan_motion.enabled || s_stackchan_motion.faulted) return false;
    if (expression != M5_PLATFORM_STACKCHAN_CELEBRATE &&
        expression != M5_PLATFORM_STACKCHAN_SAD &&
        expression != M5_PLATFORM_STACKCHAN_DANCE) return false;
    if (s_stackchan_motion.phase == StackChanMotionPhase::attention_hold ||
        s_stackchan_motion.phase == StackChanMotionPhase::attention_returning) {
        // A result expression supersedes the neutral listening return.
        s_stackchan_motion.phase = StackChanMotionPhase::idle;
    }
    if (s_stackchan_motion.phase != StackChanMotionPhase::idle) {
        /* Connection loss is higher-value body language than another track
         * celebration. Queue exactly one sadness event behind an active move. */
        if (expression == M5_PLATFORM_STACKCHAN_SAD) {
            s_stackchan_motion.queued = expression;
            s_stackchan_motion.has_queued = true;
            return true;
        }
        return false;
    }
    s_stackchan_motion.pending = expression;
    if (expression == M5_PLATFORM_STACKCHAN_DANCE) {
        s_stackchan_motion.dance_variant = esp_random() % 8;
    }
    ESP_LOGI(TAG, "Kizz gesture starting: %s",
             expression == M5_PLATFORM_STACKCHAN_DANCE ? "dance" :
             expression == M5_PLATFORM_STACKCHAN_SAD ? "sad" : "celebrate");
    if (expression == M5_PLATFORM_STACKCHAN_DANCE) {
        ESP_LOGI(TAG, "Kizz dance variation=%u",
                 s_stackchan_motion.dance_variant + 1);
    }
    if (expression == M5_PLATFORM_STACKCHAN_DANCE) {
        stackchan_dance_pose(0);
        s_stackchan_motion.deadline = stackchan_dance_deadline(0);
    } else if (expression == M5_PLATFORM_STACKCHAN_SAD) {
        /* A slow look away with the head lowered: readable body language
         * without turning a connection problem into a theatrical routine. */
        stackchan_pose(-90, 0, 240);
        s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_SAD;
        s_stackchan_motion.deadline = esp_timer_get_time() + 650000;
    } else {
        stackchan_pose(-120, 100, 340);
        s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_BEAM_LEFT;
        s_stackchan_motion.deadline = esp_timer_get_time() + 560000;
    }
    s_stackchan_motion.phase = StackChanMotionPhase::first_pose;
    return true;
}

extern "C" void m5_platform_stackchan_expression_process(void) {
    if (s_stackchan_motion.phase == StackChanMotionPhase::attention_hold)
        return;
    if (s_stackchan_motion.phase == StackChanMotionPhase::attention_returning) {
        if (esp_timer_get_time() < s_stackchan_motion.deadline) return;
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
        M5StackChan.Motion.setTorqueEnabled(false);
#endif
        s_stackchan_motion.phase = StackChanMotionPhase::idle;
        ESP_LOGI(TAG, "Kizz listening posture returned home");
        return;
    }
    if (!s_stackchan_motion.enabled || s_stackchan_motion.faulted ||
        s_stackchan_motion.phase == StackChanMotionPhase::idle ||
        esp_timer_get_time() < s_stackchan_motion.deadline) return;

    if (s_stackchan_motion.phase == StackChanMotionPhase::first_pose) {
        if (s_stackchan_motion.pending == M5_PLATFORM_STACKCHAN_DANCE) {
            stackchan_dance_pose(1);
            s_stackchan_motion.deadline = stackchan_dance_deadline(1);
        } else if (s_stackchan_motion.pending == M5_PLATFORM_STACKCHAN_SAD) {
            stackchan_pose(40, 0, 220);
            s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_SAD;
            s_stackchan_motion.deadline = esp_timer_get_time() + 620000;
        } else {
            stackchan_pose(120, 100, 340);
            s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_BEAM_RIGHT;
            s_stackchan_motion.deadline = esp_timer_get_time() + 560000;
        }
        s_stackchan_motion.phase = StackChanMotionPhase::second_pose;
        return;
    }

    if (s_stackchan_motion.phase == StackChanMotionPhase::second_pose) {
        if (s_stackchan_motion.pending == M5_PLATFORM_STACKCHAN_DANCE) {
            stackchan_dance_pose(2);
            s_stackchan_motion.phase = StackChanMotionPhase::third_pose;
            s_stackchan_motion.deadline = stackchan_dance_deadline(2);
            return;
        }
        s_stackchan_motion.phase = StackChanMotionPhase::fourth_pose;
        s_stackchan_motion.deadline = esp_timer_get_time() + 250000;
        return;
    }

    if (s_stackchan_motion.phase == StackChanMotionPhase::third_pose) {
        stackchan_dance_pose(3);
        s_stackchan_motion.phase = StackChanMotionPhase::fourth_pose;
        s_stackchan_motion.deadline = stackchan_dance_deadline(3);
        return;
    }

    if (s_stackchan_motion.phase == StackChanMotionPhase::fourth_pose) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
        M5StackChan.Motion.goHome(
            s_stackchan_motion.pending == M5_PLATFORM_STACKCHAN_DANCE ? 260 : 280);
#endif
        s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_SETTLE;
        s_stackchan_motion.phase = StackChanMotionPhase::returning;
        s_stackchan_motion.deadline = esp_timer_get_time() + 900000;
        return;
    }

    if (s_stackchan_motion.phase == StackChanMotionPhase::returning) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
        M5StackChan.Motion.setTorqueEnabled(false);
#endif
        s_stackchan_motion.phase = StackChanMotionPhase::idle;
        s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_NEUTRAL;
        ESP_LOGI(TAG, "Kizz gesture complete via official M5StackChan BSP");
        if (s_stackchan_motion.has_queued) {
            const auto queued = s_stackchan_motion.queued;
            s_stackchan_motion.has_queued = false;
            m5_platform_stackchan_expression_trigger(queued);
        }
    }
}

extern "C" bool m5_platform_stackchan_expression_faulted(void) {
    return s_stackchan_motion.faulted;
}

extern "C" m5_platform_stackchan_face_cue_t
m5_platform_stackchan_face_cue(void) {
    return s_stackchan_motion.face_cue;
}

extern "C" bool m5_platform_stackchan_sound_trigger(
    m5_platform_stackchan_sound_t sound) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    if (!s_started || s_board != M5_PLATFORM_BOARD_STACKCHAN ||
        !s_stackchan_voice.enabled ||
        sound < M5_PLATFORM_STACKCHAN_SOUND_MORE ||
        sound > M5_PLATFORM_STACKCHAN_SOUND_NEW_ROOM) return false;

    const int64_t now = esp_timer_get_time();
    size_t candidates = 0;
    for (const auto &phrase : M5_STACKCHAN_VOICE_PHRASES)
        if (phrase.sound == sound) ++candidates;
    if (!candidates) return false;

    const size_t wanted = esp_random() % candidates;
    const m5_stackchan_voice_phrase_t *selected = nullptr;
    size_t seen = 0;
    for (const auto &phrase : M5_STACKCHAN_VOICE_PHRASES) {
        if (phrase.sound != sound) continue;
        if (seen++ == wanted) { selected = &phrase; break; }
    }
    if (!selected) return false;

    const auto sound_index = static_cast<size_t>(sound);
    if (s_stackchan_voice.last_started[sound_index] &&
        now - s_stackchan_voice.last_started[sound_index] <
            static_cast<int64_t>(selected->cooldown_ms) * 1000) return false;
    if (s_stackchan_voice.phrase &&
        selected->priority < s_stackchan_voice.phrase->priority) return false;

    if (s_voice_listener_enabled && s_voice_audio_lock) {
        if (xSemaphoreTake(s_voice_audio_lock, pdMS_TO_TICKS(250)) != pdTRUE) {
            ESP_LOGW(TAG, "Kizz skipped sound: microphone handoff was busy");
            return false;
        }
        const bool resume_listening = !s_voice_waiting_for_response;
        const bool speaker_ready = voice_take_speaker();
        xSemaphoreGive(s_voice_audio_lock);
        if (!speaker_ready) return false;
        if (resume_listening) s_voice_resume_after_sound = true;
    }
    if (!M5.Speaker.isEnabled()) return false;

    s_stackchan_voice.phrase = selected;
    s_stackchan_voice.note = 0;
    s_stackchan_voice.last_started[sound_index] = now;
    ESP_LOGI(TAG, "Kizz voice: %s", selected->name);
    stackchan_voice_note(0);
    if (!s_stackchan_voice.phrase && s_voice_resume_after_sound &&
        s_voice_audio_lock &&
        xSemaphoreTake(s_voice_audio_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_voice_resume_after_sound = false;
        voice_take_microphone();
        xSemaphoreGive(s_voice_audio_lock);
    }
    return true;
#else
    (void)sound;
    return false;
#endif
}

extern "C" void m5_platform_stackchan_sound_process(void) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    if (!s_stackchan_voice.phrase ||
        esp_timer_get_time() < s_stackchan_voice.deadline) return;
    ++s_stackchan_voice.note;
    if (s_stackchan_voice.note >= s_stackchan_voice.phrase->note_count) {
        s_stackchan_voice.phrase = nullptr;
        if (s_voice_resume_after_sound && s_voice_listener_enabled &&
            s_voice_audio_lock &&
            xSemaphoreTake(s_voice_audio_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_voice_resume_after_sound = false;
            s_voice_waiting_for_response = false;
            voice_take_microphone();
            xSemaphoreGive(s_voice_audio_lock);
        }
        return;
    }
    stackchan_voice_note(s_stackchan_voice.note);
#endif
}

extern "C" bool m5_platform_stackchan_sound_enable(bool enabled) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    if (!s_started || s_board != M5_PLATFORM_BOARD_STACKCHAN ||
        !M5.Speaker.isEnabled()) return false;
    if (enabled && !M5.Speaker.isRunning() && !M5.Speaker.begin()) {
        ESP_LOGE(TAG, "Kizz sounds could not start via M5Unified");
        s_stackchan_voice.enabled = false;
        return false;
    }
    s_stackchan_voice.enabled = enabled;
    if (!enabled) {
        s_stackchan_voice.phrase = nullptr;
        M5.Speaker.stop();
    }
    ESP_LOGI(TAG, "Kizz sounds %s", enabled ? "enabled" : "disabled");
    return true;
#else
    (void)enabled;
    return false;
#endif
}

extern "C" bool m5_platform_stackchan_sound_volume(
    m5_platform_stackchan_volume_t volume) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    if (!s_started || s_board != M5_PLATFORM_BOARD_STACKCHAN ||
        !M5.Speaker.isEnabled() ||
        volume < M5_PLATFORM_STACKCHAN_VOLUME_LOW ||
        volume > M5_PLATFORM_STACKCHAN_VOLUME_HIGH) return false;
    const uint8_t gain = STACKCHAN_VOICE_GAINS[static_cast<size_t>(volume)];
    M5.Speaker.setVolume(gain);
    ESP_LOGI(TAG, "Kizz voice level=%u gain=%u",
             static_cast<unsigned>(volume), static_cast<unsigned>(gain));
    return M5.Speaker.getVolume() == gain;
#else
    (void)volume;
    return false;
#endif
}

extern "C" void m5_platform_set_brightness(uint8_t brightness) {
    if (s_started) {
        M5.Display.setBrightness(brightness);
    }
}

extern "C" void m5_platform_display_sleep(void) {
    if (s_started) {
        m5_platform_haptic(0);
        stackchan_quiesce_for_sleep();
        M5.Display.sleep();
    }
}

extern "C" void m5_platform_display_wake(void) {
    if (s_started) {
        M5.Display.wakeup();
        stackchan_resume_from_sleep();
    }
}

extern "C" void m5_platform_power_off(void) {
    if (!s_started) return;

    ESP_LOGI(TAG, "Entering %s board power-off path",
             m5_platform_board_name());
    m5_platform_haptic(0);
    stackchan_quiesce_for_sleep();
    M5.Display.setBrightness(0);
    M5.Display.sleep();
    M5.Display.waitDisplay();
    M5.Power.powerOff();

    /* The supported exact targets should never return from Power_Class. If a
     * future board profile does, fail awake through a clean boot instead of
     * continuing with stopped peripherals and a dark display. */
    ESP_LOGE(TAG, "Board power-off returned unexpectedly; rebooting");
    esp_restart();
}
