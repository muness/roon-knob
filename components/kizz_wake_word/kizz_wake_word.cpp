#include "kizz_wake_word.h"

// The managed streaming wrapper intentionally keeps its implementation small
// and does not expose a reset operation for a resident secondary model. This
// translation unit needs the two reset primitives to reuse that model safely
// between independent candidate clips; no framework ABI is changed.
#define protected public
#include "esphome/components/micro_wake_word/micro_wake_word.h"
#undef protected
#include "esphome/components/microphone/external_audio_microphone.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>

#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr float KIZZ_DEFAULT_PROBABILITY_CUTOFF = 0.70f;
// Both students use a single streaming output. The detector is deliberately
// permissive; the verifier supplies the precision gate on a frozen clip.
constexpr size_t KIZZ_DEFAULT_SLIDING_WINDOW = 1;
constexpr size_t KIZZ_TENSOR_ARENA_BYTES = 65536;
constexpr size_t KIZZ_FEATURE_STEP_SAMPLES = 160;
constexpr float KIZZ_VERIFIER_EARLY_ACCEPT_CUTOFF = 167.0f / 255.0f;
constexpr char KIZZ_DETECTOR_MODEL_SHA256[] =
    "76250d0cef49f893df4724ea6cce0e87b8a8d0d63cf10fbe23c0e624298871ff";
constexpr char KIZZ_VERIFIER_MODEL_SHA256[] =
    "4e69c38ad5967bf7bf39e83da1f3d411615f9bfae63aee988d585755fa51016d";
constexpr char KIZZ_NVS_NAMESPACE[] = "kizz_wake";
constexpr char KIZZ_NVS_CUTOFF_KEY[] = "cutoff_milli";
constexpr char KIZZ_NVS_WINDOW_KEY[] = "window";
constexpr char KIZZ_NVS_VERSION_KEY[] = "config_v";
// Version 8 selects the scalar-detector/resident-device-specialist-verifier
// cascade artifact.
constexpr uint8_t KIZZ_NVS_CONFIG_VERSION = 8;

extern const uint8_t kizz_model_start[]
    asm("_binary_hiphi_kizz_tflite_start");
extern const uint8_t kizz_verifier_model_start[]
    asm("_binary_hiphi_kizz_device_specialist_tflite_start");

const char *TAG = "kizz_wake_word";

class KizzMicroWakeWord final
    : public esphome::micro_wake_word::MicroWakeWord {
 public:
    KizzMicroWakeWord()
        : verifier_(kizz_verifier_model_start, 1.0f,
                    KIZZ_DEFAULT_SLIDING_WINDOW, "Kizz Control verifier",
                    KIZZ_TENSOR_ARENA_BYTES) {}

    ~KizzMicroWakeWord() {
        verifier_.unload_model();
    }

    void setup() override {
        esphome::micro_wake_word::MicroWakeWord::setup();
        if (this->is_failed()) return;
        // Keep the verifier interpreter and arena resident. The detector is
        // unloaded by MicroWakeWord before its callback, so this avoids the
        // candidate-path model allocation/teardown that made the old cascade
        // expensive and fragmented the heap.
        if (!verifier_.load_model(this->streaming_op_resolver_)) {
            ESP_LOGE(TAG, "Kizz verifier failed to load");
            this->mark_failed();
        }
    }

    bool evaluate_clip(const int16_t *samples, size_t sample_count,
                       float *probability) {
        using namespace esphome::micro_wake_word;
        if (!samples || !sample_count || !probability ||
            !verifier_.load_model(this->streaming_op_resolver_) ||
            !reset_verifier_state()) {
            return false;
        }

        FrontendConfig config = {};
        config.window.size_ms = FEATURE_DURATION_MS;
        config.window.step_size_ms = 10;
        config.filterbank.num_channels = PREPROCESSOR_FEATURE_SIZE;
        config.filterbank.lower_band_limit = 125.0;
        config.filterbank.upper_band_limit = 7500.0;
        config.noise_reduction.smoothing_bits = 10;
        config.noise_reduction.even_smoothing = 0.025;
        config.noise_reduction.odd_smoothing = 0.06;
        config.noise_reduction.min_signal_remaining = 0.05;
        config.pcan_gain_control.enable_pcan = 1;
        config.pcan_gain_control.strength = 0.95;
        config.pcan_gain_control.offset = 80.0;
        config.pcan_gain_control.gain_bits = 21;
        config.log_scale.enable_log = 1;
        config.log_scale.scale_shift = 6;

        FrontendState frontend = {};
        if (!FrontendPopulateState(&config, &frontend,
                                   AUDIO_SAMPLE_FREQUENCY)) {
            FrontendFreeStateContents(&frontend);
            return false;
        }

        int16_t padded[KIZZ_FEATURE_STEP_SAMPLES] = {};
        int8_t features[PREPROCESSOR_FEATURE_SIZE] = {};
        bool produced_features = false;
        float peak_probability = 0.0f;
        for (size_t offset = 0; offset < sample_count;
             offset += KIZZ_FEATURE_STEP_SAMPLES) {
            const size_t remaining = sample_count - offset;
            const size_t count = std::min(remaining,
                                          KIZZ_FEATURE_STEP_SAMPLES);
            const int16_t *window = samples + offset;
            if (count != KIZZ_FEATURE_STEP_SAMPLES) {
                memset(padded, 0, sizeof(padded));
                memcpy(padded, window, count * sizeof(int16_t));
                window = padded;
            }
            size_t samples_read = 0;
            const FrontendOutput output = FrontendProcessSamples(
                &frontend, window, KIZZ_FEATURE_STEP_SAMPLES, &samples_read);
            if (samples_read != KIZZ_FEATURE_STEP_SAMPLES) {
                FrontendFreeStateContents(&frontend);
                return false;
            }
            if (output.size == 0) continue;
            if (output.size != PREPROCESSOR_FEATURE_SIZE) {
                FrontendFreeStateContents(&frontend);
                return false;
            }
            produced_features = true;
            for (size_t feature = 0; feature < output.size; ++feature) {
                constexpr int32_t value_scale = 256;
                constexpr int32_t value_div = 666;
                const int32_t value =
                    ((output.values[feature] * value_scale) +
                     (value_div / 2)) /
                    value_div;
                features[feature] = static_cast<int8_t>(
                    std::clamp<int32_t>(value - 128, -128, 127));
            }
            if (!verifier_.perform_streaming_inference(features)) {
                FrontendFreeStateContents(&frontend);
                return false;
            }
            peak_probability = std::max(
                peak_probability, verifier_.current_probability());
            // The verifier is only a precision gate. Once it accepts, stop
            // paying inference cost for the rest of the candidate clip.
            if (peak_probability >= KIZZ_VERIFIER_EARLY_ACCEPT_CUTOFF) break;
        }
        *probability = produced_features ? peak_probability : 0.0f;
        FrontendFreeStateContents(&frontend);
        return produced_features;
    }

 private:
    bool reset_verifier_state() {
        verifier_.current_stride_step_ = 0;
        verifier_.reset_probabilities();
        if (!verifier_.interpreter_ || !verifier_.mrv_) return false;
        // MicroResourceVariables owns the streaming tensors used by this
        // model. Reset it and any ordinary variable tensors so a resident
        // candidate starts in exactly the same state as a fresh interpreter.
        if (verifier_.mrv_->ResetAll() != kTfLiteOk) return false;
        const tflite::Model *model = tflite::GetModel(verifier_.model_start_);
        for (unsigned subgraph_index = 0;
             subgraph_index < model->subgraphs()->size(); ++subgraph_index) {
            const auto *subgraph = model->subgraphs()->Get(subgraph_index);
            for (unsigned tensor_index = 0;
                 tensor_index < subgraph->tensors()->size(); ++tensor_index) {
                if (subgraph->tensors()->Get(tensor_index)->is_variable() &&
                    verifier_.interpreter_->ResetVariableTensor(
                        static_cast<int>(tensor_index),
                        static_cast<int>(subgraph_index)) != kTfLiteOk)
                    return false;
            }
        }
        return true;
    }

    esphome::micro_wake_word::WakeWordModel verifier_;
};

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
    s_wake_word->set_microphone(s_microphone);
    s_wake_word->set_features_step_size(10);
    // StackChan repeatedly hands its one I2S controller between the official
    // microphone and speaker. The verifier stays resident; the detector uses
    // the framework's normal stop/start lifecycle so its tensor arena is not
    // held while the candidate clip is verified.
    s_wake_word->add_wake_word_model(
        kizz_model_start, cutoff, KIZZ_DEFAULT_SLIDING_WINDOW,
        "Kizz Control detector", KIZZ_TENSOR_ARENA_BYTES);
    s_wake_word->add_detection_callback([](std::string) {
        s_detection_probability_milli = probability_to_milli(
            s_wake_word ? s_wake_word->get_wake_word_probability() : 0.0f);
        s_runtime_target = WakeRuntimeTarget::PAUSED;
        transition_to(WakeRuntimeState::PAUSED, "wake detected");
        ESP_LOGI(TAG, "Kizz Control detected locally");
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
    ESP_LOGI(TAG,
             "Kizz cascade ready: detector=scalar cutoff=%.2f "
             "verifier_cutoff=%.3f detector_sha256=%s verifier_sha256=%s",
             static_cast<double>(cutoff),
             static_cast<double>(KIZZ_VERIFIER_EARLY_ACCEPT_CUTOFF),
             KIZZ_DETECTOR_MODEL_SHA256, KIZZ_VERIFIER_MODEL_SHA256);
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
        cutoff_milli == static_cast<uint16_t>(
            KIZZ_DEFAULT_PROBABILITY_CUTOFF * 1000)) {
        s_probability_cutoff_milli = cutoff_milli;
    }
    if (nvs_get_u8(handle, KIZZ_NVS_WINDOW_KEY, &window) == ESP_OK &&
        window >= 1 && window <= 20) {
        s_sliding_window = window;
    }
    nvs_close(handle);
}

void detection_task(void *) {
    esp_pm_lock_handle_t cpu_frequency_lock = nullptr;
    bool cpu_frequency_lock_held = false;
    const esp_err_t lock_create_result = esp_pm_lock_create(
        ESP_PM_CPU_FREQ_MAX, 0, "kizz_mww", &cpu_frequency_lock);
    if (lock_create_result == ESP_OK) {
        ESP_LOGI(TAG,
                 "Kizz wake CPU-frequency lock ready (held only while armed)");
    } else {
        ESP_LOGW(TAG, "Kizz wake CPU-frequency lock unavailable: %s",
                 esp_err_to_name(lock_create_result));
    }
    for (;;) {
        if (s_reconfigure_requested.exchange(false)) {
            const float cutoff =
                s_probability_cutoff_milli.load() / 1000.0f;
            s_wake_word->set_wake_word_model_parameters(
                cutoff, KIZZ_DEFAULT_SLIDING_WINDOW);
            ESP_LOGI(TAG,
                     "Kizz Control detector cutoff updated: cutoff=%.2f",
                     static_cast<double>(cutoff));
        }
        if (!s_wake_word) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        const WakeRuntimeTarget target = s_runtime_target.load();
        const WakeRuntimeState state = s_runtime_state.load();
        const bool needs_full_cpu = target == WakeRuntimeTarget::ARMED;
        if (cpu_frequency_lock && needs_full_cpu &&
            !cpu_frequency_lock_held) {
            const esp_err_t result = esp_pm_lock_acquire(cpu_frequency_lock);
            if (result == ESP_OK) {
                cpu_frequency_lock_held = true;
            } else {
                ESP_LOGE(TAG, "Kizz wake CPU-frequency lock acquire failed: %s",
                         esp_err_to_name(result));
            }
        } else if (cpu_frequency_lock && !needs_full_cpu &&
                   cpu_frequency_lock_held) {
            const esp_err_t result = esp_pm_lock_release(cpu_frequency_lock);
            if (result == ESP_OK) {
                cpu_frequency_lock_held = false;
            } else {
                ESP_LOGE(TAG, "Kizz wake CPU-frequency lock release failed: %s",
                         esp_err_to_name(result));
            }
        }
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
            ESP_LOGI(TAG,
                     "Kizz Control score: current=%.3f peak=%.3f "
                     "cutoff=%.3f state=%s",
                     static_cast<double>(probability),
                     static_cast<double>(s_probability_milli.load() / 1000.0f),
                     static_cast<double>(s_probability_cutoff_milli.load() / 1000.0f),
                     kizz_wake_word_runtime_state());
        }
        // At 1 kHz this yields without sleeping long enough to starve the
        // official M5Unified microphone capture task on the same core.
        vTaskDelay(1);
    }
}
}  // namespace

extern "C" bool kizz_wake_word_start(kizz_wake_word_detected_cb_t detected_cb) {
    if (s_wake_word) return true;
    // This adapter is fed exclusively by M5.Mic.record() in m5_platform.cpp;
    // it does not open or own a second physical microphone.
    s_microphone = new (std::nothrow)
        esphome::microphone::ExternalAudioMicrophone(16384);
    if (!s_microphone || !s_microphone->is_ready()) {
        ESP_LOGE(TAG, "Kizz StackChan microphone adapter allocation failed");
        delete s_microphone;
        s_microphone = nullptr;
        return false;
    }
    s_detected_cb = detected_cb;
    s_runtime_target = WakeRuntimeTarget::ARMED;
    s_runtime_state = WakeRuntimeState::STARTING;
    load_persisted_config();
    if (!create_wake_word_model()) {
        ESP_LOGE(TAG, "Kizz Control student failed to start");
        delete s_microphone;
        s_microphone = nullptr;
        s_detected_cb = nullptr;
        return false;
    }
    if (xTaskCreatePinnedToCore(detection_task, "kizz_mww", 6144, nullptr, 6,
                                nullptr, 1) != pdPASS) {
        ESP_LOGE(TAG, "Kizz Control wake task creation failed");
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

extern "C" bool kizz_wake_word_verify_clip(const int16_t *samples,
                                             size_t sample_count,
                                             float *probability) {
    return s_wake_word &&
        s_wake_word->evaluate_clip(samples, sample_count, probability);
}

extern "C" bool kizz_wake_word_configure(float probability_cutoff,
                                           size_t sliding_window) {
    if (!std::isfinite(probability_cutoff) || probability_cutoff < 0.0f ||
        probability_cutoff > 1.0f) return false;
    const uint16_t cutoff_milli =
        static_cast<uint16_t>(probability_cutoff * 1000.0f + 0.5f);
    // This student was evaluated only at the generated raw-score threshold.
    // Keep the legacy protocol shape but reject sensitivity drift; a future
    // adjustable operating point must carry its own evaluation evidence.
    if (cutoff_milli != static_cast<uint16_t>(
            KIZZ_DEFAULT_PROBABILITY_CUTOFF * 1000) ||
        sliding_window != KIZZ_DEFAULT_SLIDING_WINDOW) return false;

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
