#include "kizz_wake_word.h"

#include "esphome/components/micro_wake_word/micro_wake_word.h"
#include "esphome/components/microphone/external_audio_microphone.h"

#include <atomic>
#include <new>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr float KIZZ_PROBABILITY_CUTOFF = 0.73f;
constexpr size_t KIZZ_SLIDING_WINDOW = 5;
constexpr size_t KIZZ_TENSOR_ARENA_BYTES = 40000;

extern const uint8_t kizz_model_start[]
    asm("_binary_hiphi_kizz_tflite_start");

const char *TAG = "kizz_wake_word";
esphome::microphone::ExternalAudioMicrophone *s_microphone = nullptr;
esphome::micro_wake_word::MicroWakeWord *s_wake_word = nullptr;
kizz_wake_word_detected_cb_t s_detected_cb = nullptr;
std::atomic_bool s_pause_requested{false};
std::atomic_bool s_resume_requested{false};

void detection_task(void *) {
    for (;;) {
        if (s_pause_requested.exchange(false) && s_wake_word->is_running())
            s_wake_word->stop();
        if (s_resume_requested.exchange(false) && !s_wake_word->is_running())
            s_wake_word->start();
        s_wake_word->loop();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
}  // namespace

extern "C" bool kizz_wake_word_start(kizz_wake_word_detected_cb_t detected_cb) {
    if (s_wake_word) return true;
    s_microphone = new (std::nothrow)
        esphome::microphone::ExternalAudioMicrophone(8192);
    s_wake_word = new (std::nothrow) esphome::micro_wake_word::MicroWakeWord();
    if (!s_microphone || !s_microphone->is_ready() || !s_wake_word) {
        ESP_LOGE(TAG, "Kizz wake runtime allocation failed");
        delete s_wake_word;
        delete s_microphone;
        s_wake_word = nullptr;
        s_microphone = nullptr;
        return false;
    }
    s_detected_cb = detected_cb;
    s_wake_word->set_microphone(s_microphone);
    s_wake_word->set_features_step_size(10);
    s_wake_word->add_wake_word_model(
        kizz_model_start, KIZZ_PROBABILITY_CUTOFF, KIZZ_SLIDING_WINDOW,
        "HiPhi Kizz", KIZZ_TENSOR_ARENA_BYTES);
    s_wake_word->add_detection_callback([](std::string) {
        ESP_LOGI(TAG, "HiPhi Kizz detected locally");
        if (s_detected_cb) s_detected_cb();
    });
    s_wake_word->setup();
    if (s_wake_word->is_failed()) {
        ESP_LOGE(TAG, "Kizz wake model setup failed");
        delete s_wake_word;
        delete s_microphone;
        s_wake_word = nullptr;
        s_microphone = nullptr;
        s_detected_cb = nullptr;
        return false;
    }
    s_wake_word->start();
    if (s_wake_word->status_has_error() || !s_wake_word->is_running()) {
        ESP_LOGE(TAG, "Kizz wake model failed to start");
        delete s_wake_word;
        delete s_microphone;
        s_wake_word = nullptr;
        s_microphone = nullptr;
        s_detected_cb = nullptr;
        return false;
    }
    if (xTaskCreatePinnedToCore(detection_task, "kizz_mww", 6144, nullptr, 5,
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
    ESP_LOGI(TAG, "HiPhi Kizz model ready: cutoff=%.2f window=%u",
             static_cast<double>(KIZZ_PROBABILITY_CUTOFF),
             static_cast<unsigned>(KIZZ_SLIDING_WINDOW));
    return true;
}

extern "C" size_t kizz_wake_word_feed(const int16_t *samples,
                                       size_t sample_count) {
    return s_microphone ? s_microphone->feed(samples, sample_count) : 0;
}

extern "C" void kizz_wake_word_pause(void) {
    if (s_wake_word) s_pause_requested = true;
}

extern "C" void kizz_wake_word_resume(void) {
    if (s_wake_word) s_resume_requested = true;
}
