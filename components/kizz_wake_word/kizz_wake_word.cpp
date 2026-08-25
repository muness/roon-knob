#include "kizz_wake_word.h"

#include "esphome/components/micro_wake_word/micro_wake_word.h"
#include "esphome/components/microphone/external_audio_microphone.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr float KIZZ_DEFAULT_PROBABILITY_CUTOFF = 0.70f;
// This model emits a short, high-confidence pulse for a natural Kizz utterance.
// Averaging five inference outputs diluted a measured 0.831 pulse below the
// cutoff and caused a false reject even from a fresh boot.
constexpr size_t KIZZ_DEFAULT_SLIDING_WINDOW = 1;
constexpr size_t KIZZ_TENSOR_ARENA_BYTES = 65536;
constexpr char KIZZ_NVS_NAMESPACE[] = "kizz_wake";
constexpr char KIZZ_NVS_CUTOFF_KEY[] = "cutoff_milli";
constexpr char KIZZ_NVS_WINDOW_KEY[] = "window";
constexpr char KIZZ_NVS_VERSION_KEY[] = "config_v";
// Version 3 resets devices that retained the earlier specialist-model .80
// operating point. The ordered-state model is qualified at .70.
constexpr uint8_t KIZZ_NVS_CONFIG_VERSION = 3;

extern const uint8_t kizz_ordered_model_start[]
    asm("_binary_hiphi_kizz_ordered_tflite_start");

class KizzMicroWakeWord final
    : public esphome::micro_wake_word::MicroWakeWord {
};

const char *TAG = "kizz_wake_word";
esphome::microphone::ExternalAudioMicrophone *s_microphone = nullptr;
KizzMicroWakeWord *s_wake_word = nullptr;
kizz_wake_word_detected_cb_t s_detected_cb = nullptr;
enum class WakeRuntimeState : uint8_t {
    PAUSED,
    STARTING,
    ARMED,
    STOPPING,
    FAULTED,
};

enum class WakeRuntimeTarget : uint8_t {
    PAUSED,
    ARMED,
};

std::atomic<WakeRuntimeState> s_runtime_state{WakeRuntimeState::STARTING};
// Target state persists across asynchronous STARTING/STOPPING transitions.
std::atomic<WakeRuntimeTarget> s_runtime_target{WakeRuntimeTarget::ARMED};
std::atomic<uint32_t> s_transition_count{0};
std::atomic<uint16_t> s_probability_milli{0};
std::atomic<uint16_t> s_detection_probability_milli{0};
std::atomic<int64_t> s_probability_peak_at_us{0};
int64_t s_probability_log_at_us = 0;
std::atomic_bool s_reconfigure_requested{false};
std::atomic<uint16_t> s_probability_cutoff_milli{
    static_cast<uint16_t>(KIZZ_DEFAULT_PROBABILITY_CUTOFF * 1000)};
std::atomic<size_t> s_sliding_window{KIZZ_DEFAULT_SLIDING_WINDOW};

uint16_t probability_to_milli(float probability) {
    return static_cast<uint16_t>(
        std::max(0.0f, std::min(1.0f, probability)) * 1000.0f + 0.5f);
}

const char *runtime_state_name(WakeRuntimeState state) {
    switch (state) {
        case WakeRuntimeState::PAUSED: return "paused";
        case WakeRuntimeState::STARTING: return "starting";
        case WakeRuntimeState::ARMED: return "armed";
        case WakeRuntimeState::STOPPING: return "stopping";
        case WakeRuntimeState::FAULTED: return "faulted";
    }
    return "unknown";
}

bool transition_allowed(WakeRuntimeState from, WakeRuntimeState to) {
    if (to == WakeRuntimeState::FAULTED) return true;
    switch (from) {
        case WakeRuntimeState::PAUSED:
            return to == WakeRuntimeState::STARTING;
        case WakeRuntimeState::STARTING:
            return to == WakeRuntimeState::ARMED ||
                   to == WakeRuntimeState::STOPPING;
        case WakeRuntimeState::ARMED:
            return to == WakeRuntimeState::STOPPING ||
                   to == WakeRuntimeState::PAUSED;
        case WakeRuntimeState::STOPPING:
            return to == WakeRuntimeState::PAUSED;
        case WakeRuntimeState::FAULTED:
            return false;
    }
    return false;
}

void transition_to(WakeRuntimeState next, const char *reason) {
    const WakeRuntimeState previous = s_runtime_state.load();
    if (previous == next) return;
    if (!transition_allowed(previous, next)) {
        ESP_LOGE(TAG, "Invalid wake transition %s -> %s (%s)",
                 runtime_state_name(previous), runtime_state_name(next), reason);
        s_runtime_state = WakeRuntimeState::FAULTED;
        ++s_transition_count;
        return;
    }
    s_runtime_state = next;
    ++s_transition_count;
    ESP_LOGI(TAG, "Wake transition %s -> %s (%s)",
             runtime_state_name(previous), runtime_state_name(next), reason);
}

bool create_wake_word_model() {
    s_wake_word = new (std::nothrow) KizzMicroWakeWord();
    if (!s_wake_word) return false;

    const float cutoff = s_probability_cutoff_milli.load() / 1000.0f;
    const size_t window = s_sliding_window.load();
    s_wake_word->set_microphone(s_microphone);
    s_wake_word->set_features_step_size(10);
    s_wake_word->add_ordered_state_model(
        kizz_ordered_model_start, cutoff, window, "HiPhi Kizz",
        KIZZ_TENSOR_ARENA_BYTES);
    s_wake_word->add_detection_callback([](std::string) {
        // MicroWakeWord invokes this callback from loop() before the loop's
        // later telemetry update. Preserve the detector value at this exact
        // point so evidence metadata cannot report a stale recent peak.
        s_detection_probability_milli = probability_to_milli(
            s_wake_word ? s_wake_word->get_wake_word_probability() : 0.0f);
        // Detection deliberately disarms the runtime until command capture has
        // finished. This also prevents an idle detector from immediately
        // restarting in the gap before the voice task observes the callback.
        s_runtime_target = WakeRuntimeTarget::PAUSED;
        transition_to(WakeRuntimeState::PAUSED, "wake detected");
        ESP_LOGI(TAG, "HiPhi Kizz detected locally");
        if (s_detected_cb) s_detected_cb();
    });
    s_wake_word->setup();
    if (s_wake_word->is_failed()) {
        delete s_wake_word;
        s_wake_word = nullptr;
        return false;
    }
    s_wake_word->start();
    if (s_wake_word->status_has_error() || !s_wake_word->is_running()) {
        delete s_wake_word;
        s_wake_word = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "HiPhi Kizz ordered-state model ready: cutoff=%.2f window=%u",
             static_cast<double>(cutoff), static_cast<unsigned>(window));
    return true;
}

void load_persisted_config() {
    nvs_handle_t handle;
    if (nvs_open(KIZZ_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t version = 0;
    const bool current_config =
        nvs_get_u8(handle, KIZZ_NVS_VERSION_KEY, &version) == ESP_OK &&
        version == KIZZ_NVS_CONFIG_VERSION;
    nvs_close(handle);
    if (!current_config) {
        s_probability_cutoff_milli =
            static_cast<uint16_t>(KIZZ_DEFAULT_PROBABILITY_CUTOFF * 1000);
        s_sliding_window = KIZZ_DEFAULT_SLIDING_WINDOW;
        if (nvs_open(KIZZ_NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
            nvs_set_u8(handle, KIZZ_NVS_VERSION_KEY, KIZZ_NVS_CONFIG_VERSION);
            nvs_set_u16(handle, KIZZ_NVS_CUTOFF_KEY,
                        static_cast<uint16_t>(KIZZ_DEFAULT_PROBABILITY_CUTOFF * 1000));
            nvs_set_u8(handle, KIZZ_NVS_WINDOW_KEY,
                       static_cast<uint8_t>(KIZZ_DEFAULT_SLIDING_WINDOW));
            nvs_commit(handle);
            nvs_close(handle);
        }
        return;
    }
    if (nvs_open(KIZZ_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    uint16_t cutoff_milli = 0;
    uint8_t window = 0;
    if (nvs_get_u16(handle, KIZZ_NVS_CUTOFF_KEY, &cutoff_milli) == ESP_OK &&
        cutoff_milli >= 100 && cutoff_milli <= 990) {
        s_probability_cutoff_milli = cutoff_milli;
    }
    if (nvs_get_u8(handle, KIZZ_NVS_WINDOW_KEY, &window) == ESP_OK &&
        window >= 1 && window <= 20) {
        s_sliding_window = window;
    }
    nvs_close(handle);
}

void detection_task(void *) {
    for (;;) {
        if (s_reconfigure_requested.exchange(false)) {
            const float cutoff =
                s_probability_cutoff_milli.load() / 1000.0f;
            const size_t window = s_sliding_window.load();
            s_wake_word->set_wake_word_model_parameters(cutoff, window);
            ESP_LOGI(TAG,
                     "HiPhi Kizz model updated in place: cutoff=%.2f window=%u",
                     static_cast<double>(cutoff),
                     static_cast<unsigned>(window));
        }
        if (!s_wake_word) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        const WakeRuntimeTarget target = s_runtime_target.load();
        const WakeRuntimeState state = s_runtime_state.load();
        const auto engine_state = s_wake_word->get_state();

        if (state == WakeRuntimeState::STARTING &&
            engine_state == esphome::micro_wake_word::DETECTING_WAKE_WORD) {
            transition_to(WakeRuntimeState::ARMED, "engine detecting");
        } else if (state == WakeRuntimeState::STOPPING &&
                   engine_state == esphome::micro_wake_word::IDLE) {
            transition_to(WakeRuntimeState::PAUSED, "engine idle");
        }

        const WakeRuntimeState reconciled = s_runtime_state.load();
        if (target == WakeRuntimeTarget::PAUSED &&
            (reconciled == WakeRuntimeState::ARMED ||
             reconciled == WakeRuntimeState::STARTING)) {
            s_wake_word->stop();
            transition_to(WakeRuntimeState::STOPPING, "pause requested");
        } else if (target == WakeRuntimeTarget::ARMED &&
                   reconciled == WakeRuntimeState::PAUSED) {
            s_wake_word->start();
            if (s_wake_word->status_has_error())
                transition_to(WakeRuntimeState::FAULTED, "engine start failed");
            else
                transition_to(WakeRuntimeState::STARTING, "arm requested");
        }
        s_wake_word->loop();
        const float probability = s_wake_word->get_wake_word_probability();
        const uint16_t probability_milli = probability_to_milli(probability);
        const int64_t now = esp_timer_get_time();
        const uint16_t recent_peak = s_probability_milli.load();
        if (probability_milli >= recent_peak ||
            now - s_probability_peak_at_us.load() >= 2000000) {
            s_probability_milli = probability_milli;
            s_probability_peak_at_us = now;
        }
        if (now - s_probability_log_at_us >= 500000) {
            s_probability_log_at_us = now;
            ESP_LOGI(TAG, "Wake probability: current=%.3f peak=%.3f cutoff=%.3f state=%s",
                     static_cast<double>(probability),
                     static_cast<double>(s_probability_milli.load() / 1000.0f),
                     static_cast<double>(s_probability_cutoff_milli.load() / 1000.0f),
                     kizz_wake_word_runtime_state());
        }
        // One RTOS tick is intentional. At the configured tick rate,
        // pdMS_TO_TICKS(1) rounds to zero and a higher-priority detector would
        // starve the AFE task instead of yielding between inference passes.
        vTaskDelay(1);
    }
}
}  // namespace

extern "C" bool kizz_wake_word_start(kizz_wake_word_detected_cb_t detected_cb) {
    if (s_wake_word) return true;
    // Keep roughly half a second of 16 kHz mono PCM so short ESP-SR scheduling
    // bursts do not erase the wake phrase before microWakeWord can consume it.
    // This FreeRTOS stream buffer uses internal RAM; 32 KiB is not contiguous
    // after ESP-SR initializes on CoreS3, while 16 KiB is hardware-proven.
    s_microphone = new (std::nothrow)
        esphome::microphone::ExternalAudioMicrophone(16384);
    if (!s_microphone || !s_microphone->is_ready()) {
        ESP_LOGE(TAG, "Kizz wake runtime allocation failed");
        delete s_microphone;
        s_microphone = nullptr;
        return false;
    }
    s_detected_cb = detected_cb;
    s_runtime_target = WakeRuntimeTarget::ARMED;
    s_runtime_state = WakeRuntimeState::STARTING;
    load_persisted_config();
    if (!create_wake_word_model()) {
        ESP_LOGE(TAG, "Kizz wake model failed to start");
        delete s_microphone;
        s_microphone = nullptr;
        s_detected_cb = nullptr;
        return false;
    }
    // Wake inference must outrank the continuous AFE fetch task on core 1.
    // Equal priority caused sustained input overflow while the UI still said
    // ARMED, which presented as intermittent real-world recall.
    if (xTaskCreatePinnedToCore(detection_task, "kizz_mww", 6144, nullptr, 6,
                                nullptr, 1) != pdPASS) {
        ESP_LOGE(TAG, "Kizz wake task creation failed");
        s_wake_word->stop();
        delete s_wake_word;
        delete s_microphone;
        s_wake_word = nullptr;
        s_microphone = nullptr;
        s_detected_cb = nullptr;
        return false;
    }
    return true;
}

extern "C" size_t kizz_wake_word_feed(const int16_t *samples,
                                       size_t sample_count) {
    return s_microphone ? s_microphone->feed(samples, sample_count) : 0;
}

extern "C" void kizz_wake_word_pause(void) {
    if (s_wake_word) {
        s_runtime_target = WakeRuntimeTarget::PAUSED;
        ESP_LOGI(TAG, "Wake target requested: paused (state=%s)",
                 runtime_state_name(s_runtime_state.load()));
    }
}

extern "C" void kizz_wake_word_resume(void) {
    if (s_microphone) {
        s_runtime_target = WakeRuntimeTarget::ARMED;
        ESP_LOGI(TAG, "Wake target requested: armed (state=%s)",
                 runtime_state_name(s_runtime_state.load()));
    }
}

extern "C" const char *kizz_wake_word_runtime_state(void) {
    return runtime_state_name(s_runtime_state.load());
}

extern "C" uint32_t kizz_wake_word_transition_count(void) {
    return s_transition_count.load();
}

extern "C" float kizz_wake_word_probability(void) {
    return s_probability_milli.load() / 1000.0f;
}

extern "C" float kizz_wake_word_detection_probability(void) {
    return s_detection_probability_milli.load() / 1000.0f;
}

extern "C" bool kizz_wake_word_configure(float probability_cutoff,
                                           size_t sliding_window) {
    if (probability_cutoff < 0.10f || probability_cutoff > 0.99f ||
        sliding_window < 1 || sliding_window > 20) return false;

    const uint16_t cutoff_milli =
        static_cast<uint16_t>(probability_cutoff * 1000.0f + 0.5f);
    nvs_handle_t handle;
    if (nvs_open(KIZZ_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        return false;
    esp_err_t err = nvs_set_u16(handle, KIZZ_NVS_CUTOFF_KEY, cutoff_milli);
    if (err == ESP_OK)
        err = nvs_set_u8(handle, KIZZ_NVS_WINDOW_KEY,
                         static_cast<uint8_t>(sliding_window));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return false;

    s_probability_cutoff_milli = cutoff_milli;
    s_sliding_window = sliding_window;
    s_reconfigure_requested = true;
    return true;
}

extern "C" void kizz_wake_word_get_config(float *probability_cutoff,
                                            size_t *sliding_window) {
    if (probability_cutoff)
        *probability_cutoff = s_probability_cutoff_milli.load() / 1000.0f;
    if (sliding_window) *sliding_window = s_sliding_window.load();
}
