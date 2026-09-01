#include "kizz_wake_word.h"
#include "kizz_detector_aot.h"
#include "kizz_verifier_aot.h"

#define protected public
#include "esphome/components/micro_wake_word/micro_wake_word.h"
#undef protected
#include "esphome/components/microphone/external_audio_microphone.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "esphome/components/micro_wake_word/generic_ordered_state_decoder.h"
#include "esphome/components/micro_wake_word/ordered_state_stream_primer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {
constexpr float KIZZ_DEFAULT_PROBABILITY_CUTOFF = 0.70f;
constexpr size_t KIZZ_DEFAULT_SLIDING_WINDOW = 1;
// Keep only the continuously-running detector in scarce internal RAM. The
// ordered verifier is a rare third-stage operation and can keep its persistent
// state in PSRAM without starving the networking and command tasks.
constexpr size_t KIZZ_DETECTOR_TENSOR_ARENA_BYTES = 16 * 1024;
constexpr size_t KIZZ_ORDERED_VERIFIER_AOT_ARENA_BYTES = 16 * 1024;
constexpr size_t KIZZ_COMPACT_VERIFIER_AOT_ARENA_BYTES = 96 * 1024;
constexpr size_t KIZZ_VERIFIER_FRAMES = 260;
constexpr size_t KIZZ_VERIFIER_BINS = 40;
constexpr size_t KIZZ_VERIFIER_VALUES =
    KIZZ_VERIFIER_FRAMES * KIZZ_VERIFIER_BINS;
constexpr size_t KIZZ_VERIFIER_POST_CONTEXT_FRAMES = 39;
constexpr uint32_t KIZZ_DETECTOR_HOP_BUDGET_US = 10000;
constexpr size_t KIZZ_AUDIO_QUEUE_CAPACITY_BYTES = 16384;
constexpr float KIZZ_DETECTOR_RAW_SCORE_THRESHOLD = -18.20059454471544f;
constexpr float KIZZ_COMPACT_VERIFIER_RAW_SCORE_THRESHOLD =
    0.0f;
constexpr float KIZZ_ORDERED_VERIFIER_RAW_SCORE_THRESHOLD =
    -19.326665980378795f;
constexpr size_t KIZZ_ORDERED_VERIFIER_CALLS = 87;
constexpr size_t KIZZ_ORDERED_VERIFIER_WARMUP_CALLS = 21;
constexpr size_t KIZZ_ORDERED_VERIFIER_STRIDE = 3;
constexpr size_t KIZZ_ORDERED_VERIFIER_PHASE_OFFSET = 2;
constexpr float KIZZ_FRONTEND_FEATURE_SCALE = 26.0f / 256.0f;
constexpr char KIZZ_DETECTOR_MODEL_SHA256[] =
    "f07d2c010fba020e923c23734e54ba8e86751dfd1b0f23a018eb5ff79b969ae3";
constexpr char KIZZ_COMPACT_VERIFIER_MODEL_SHA256[] =
    "a28e8c8f3fe51ea3ae3fc76f0d79f2abdb06f19c71d7f26e0f08a16464025710";
constexpr char KIZZ_ORDERED_VERIFIER_MODEL_SHA256[] =
    "956a444d11f802e7780dcd3af6f43551a1fe4601fdacfd7b153bba8e11c48933";
constexpr char KIZZ_NVS_NAMESPACE[] = "kizz_wake";
constexpr char KIZZ_NVS_CUTOFF_KEY[] = "cutoff_milli";
constexpr char KIZZ_NVS_WINDOW_KEY[] = "window";
constexpr char KIZZ_NVS_VERSION_KEY[] = "config_v";
// Version 18 selects the recovered v5c detector, v10 compact gate, and ordered
// verifier. All three use fixed AOT schedules; the expensive third stage runs
// only after the compact gate accepts.
constexpr uint8_t KIZZ_NVS_CONFIG_VERSION = 18;

extern const uint8_t kizz_model_start[]
    asm("_binary_kizz_control_detector_tflite_start");
extern const uint8_t kizz_compact_verifier_model_start[]
    asm("_binary_kizz_control_compact_verifier_int8_v10_tflite_start");
extern const uint8_t kizz_ordered_verifier_model_start[]
    asm("_binary_kizz_control_ordered_verifier_int8_tflite_start");

const char *TAG = "kizz_wake_word";
uint8_t *s_detector_fast_arena = nullptr;

template <size_t BucketCount, uint32_t BucketWidthUs>
class LatencyHistogram {
 public:
    void record(uint32_t elapsed_us) {
        const size_t bucket = std::min<size_t>(
            elapsed_us / BucketWidthUs, BucketCount);
        buckets_[bucket].fetch_add(1, std::memory_order_relaxed);
        samples_.fetch_add(1, std::memory_order_relaxed);
        uint32_t total = total_us_.load(std::memory_order_relaxed);
        while (total != std::numeric_limits<uint32_t>::max()) {
            const uint32_t next = elapsed_us >
                    std::numeric_limits<uint32_t>::max() - total
                ? std::numeric_limits<uint32_t>::max()
                : total + elapsed_us;
            if (total_us_.compare_exchange_weak(
                    total, next, std::memory_order_relaxed,
                    std::memory_order_relaxed))
                break;
        }
        uint32_t previous = max_us_.load(std::memory_order_relaxed);
        while (previous < elapsed_us &&
               !max_us_.compare_exchange_weak(
                   previous, elapsed_us, std::memory_order_relaxed,
                   std::memory_order_relaxed)) {
        }
    }

    uint32_t total_us() const {
        return total_us_.load(std::memory_order_relaxed);
    }

    kizz_latency_summary_t summary() const {
        const uint32_t samples = samples_.load(std::memory_order_relaxed);
        return {
            samples,
            percentile(samples, 50),
            percentile(samples, 95),
            percentile(samples, 99),
            max_us_.load(std::memory_order_relaxed),
        };
    }

 private:
    uint32_t percentile(uint32_t samples, uint32_t percentage) const {
        if (samples == 0) return 0;
        const uint64_t target =
            (static_cast<uint64_t>(samples) * percentage + 99) / 100;
        uint64_t cumulative = 0;
        for (size_t bucket = 0; bucket <= BucketCount; ++bucket) {
            cumulative += buckets_[bucket].load(std::memory_order_relaxed);
            if (cumulative >= target) {
                if (bucket == BucketCount)
                    return max_us_.load(std::memory_order_relaxed);
                return static_cast<uint32_t>((bucket + 1) * BucketWidthUs);
            }
        }
        return max_us_.load(std::memory_order_relaxed);
    }

    std::array<std::atomic<uint32_t>, BucketCount + 1> buckets_{};
    std::atomic<uint32_t> samples_{0};
    // Saturates after roughly 71 minutes at 100% duty, safely beyond the
    // required 30-minute qualification soak.
    std::atomic<uint32_t> total_us_{0};
    std::atomic<uint32_t> max_us_{0};
};

// 250 us resolution through 30 ms (three detector hop budgets). Candidate totals
// use 20 ms resolution through a little over five seconds.
LatencyHistogram<120, 250> s_detector_hop_latency;
LatencyHistogram<120, 250> s_detector_frontend_latency;
LatencyHistogram<120, 250> s_detector_invoke_latency;
LatencyHistogram<256, 20000> s_verifier_candidate_latency;
LatencyHistogram<120, 250> s_verifier_frontend_latency;
LatencyHistogram<120, 250> s_verifier_invoke_latency;
LatencyHistogram<256, 20000> s_compact_verifier_latency;
LatencyHistogram<256, 20000> s_ordered_verifier_latency;
std::atomic<uint32_t> s_detector_candidates{0};
std::atomic<uint32_t> s_verifier_runs{0};
std::atomic<uint32_t> s_verifier_accepts{0};
std::atomic<uint32_t> s_verifier_rejects{0};
std::atomic<uint32_t> s_verifier_errors{0};
std::atomic<uint32_t> s_verifier_early_exits{0};
std::atomic<uint32_t> s_verifier_full_windows{0};
std::atomic<uint32_t> s_verifier_feature_frames{0};
std::atomic<uint32_t> s_verifier_model_invocations{0};
std::atomic<uint32_t> s_verifier_max_feature_frames{0};
std::atomic<uint32_t> s_verifier_max_model_invocations{0};
std::atomic<uint32_t> s_compact_verifier_runs{0};
std::atomic<uint32_t> s_compact_verifier_accepts{0};
std::atomic<uint32_t> s_compact_verifier_rejects{0};
std::atomic<uint32_t> s_compact_verifier_errors{0};
std::atomic<uint32_t> s_ordered_verifier_runs{0};
std::atomic<uint32_t> s_ordered_verifier_accepts{0};
std::atomic<uint32_t> s_ordered_verifier_rejects{0};
std::atomic<uint32_t> s_ordered_verifier_errors{0};
std::atomic<uint32_t> s_ordered_verifier_model_invocations{0};
std::atomic<uint32_t> s_detector_hops_over_80_percent_budget{0};
std::atomic<uint32_t> s_detector_hops_over_budget{0};
int64_t s_perf_started_at_us = 0;
int64_t s_perf_log_at_us = 0;
esp_pm_lock_handle_t s_inference_cpu_lock = nullptr;
bool s_inference_cpu_lock_held = false;

void inference_cpu_boost_begin() {
    if (!s_inference_cpu_lock || s_inference_cpu_lock_held) return;
    if (esp_pm_lock_acquire(s_inference_cpu_lock) == ESP_OK)
        s_inference_cpu_lock_held = true;
}

void inference_cpu_boost_end() {
    if (!s_inference_cpu_lock || !s_inference_cpu_lock_held) return;
    if (esp_pm_lock_release(s_inference_cpu_lock) == ESP_OK)
        s_inference_cpu_lock_held = false;
}

void update_max(std::atomic<uint32_t> &destination, uint32_t value) {
    uint32_t previous = destination.load(std::memory_order_relaxed);
    while (previous < value &&
           !destination.compare_exchange_weak(
               previous, value, std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

void detector_performance_callback(
    void *, esphome::micro_wake_word::PerformanceEvent event,
    uint32_t elapsed_us) {
    using esphome::micro_wake_word::PerformanceEvent;
    if (event == PerformanceEvent::FRONTEND) {
        s_detector_frontend_latency.record(elapsed_us);
    } else if (event == PerformanceEvent::MODEL_INVOKE) {
        s_detector_invoke_latency.record(elapsed_us);
    } else if (event == PerformanceEvent::PIPELINE_HOP) {
        s_detector_hop_latency.record(elapsed_us);
        if (elapsed_us > KIZZ_DETECTOR_HOP_BUDGET_US * 8 / 10)
            s_detector_hops_over_80_percent_budget.fetch_add(
                1, std::memory_order_relaxed);
        if (elapsed_us > KIZZ_DETECTOR_HOP_BUDGET_US)
            s_detector_hops_over_budget.fetch_add(
                1, std::memory_order_relaxed);
    }
}

class KizzMicroWakeWord final
    : public esphome::micro_wake_word::MicroWakeWord {
 public:
    ~KizzMicroWakeWord() {
        if (compact_verifier_aot_arena_)
            heap_caps_free(compact_verifier_aot_arena_);
        if (ordered_verifier_aot_arena_)
            heap_caps_free(ordered_verifier_aot_arena_);
        if (feature_ring_)
            heap_caps_free(feature_ring_);
        if (detector_aot_arena_)
            heap_caps_free(detector_aot_arena_);
    }

    void setup() override {
        detector_aot_arena_ = s_detector_fast_arena;
        s_detector_fast_arena = nullptr;
        if (!detector_aot_arena_) {
            detector_aot_arena_ = static_cast<uint8_t *>(heap_caps_malloc(
                KIZZ_DETECTOR_TENSOR_ARENA_BYTES,
                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        }
        if (!detector_aot_arena_) {
            ESP_LOGE(TAG,
                     "Kizz detector fast arena allocation failed: bytes=%u "
                     "internal_free=%u largest=%u",
                     static_cast<unsigned>(KIZZ_DETECTOR_TENSOR_ARENA_BYTES),
                     static_cast<unsigned>(heap_caps_get_free_size(
                         MALLOC_CAP_INTERNAL)),
                     static_cast<unsigned>(heap_caps_get_largest_free_block(
                         MALLOC_CAP_INTERNAL)));
            this->mark_failed();
            return;
        }
        detector_model_ = tflite::GetModel(kizz_model_start);
        if (!detector_aot_.initialize(
                detector_model_, detector_aot_arena_,
                KIZZ_DETECTOR_TENSOR_ARENA_BYTES)) {
            ESP_LOGE(TAG, "Kizz qualified detector AOT setup failed");
            this->mark_failed();
            return;
        }
        ordered_verifier_model_ =
            tflite::GetModel(kizz_ordered_verifier_model_start);
        ordered_verifier_aot_arena_ = static_cast<uint8_t *>(heap_caps_malloc(
            KIZZ_ORDERED_VERIFIER_AOT_ARENA_BYTES,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!ordered_verifier_model_ ||
            ordered_verifier_model_->version() != TFLITE_SCHEMA_VERSION ||
            !ordered_verifier_aot_arena_ ||
            !ordered_verifier_aot_.initialize(
                ordered_verifier_model_,
                ordered_verifier_aot_arena_,
                KIZZ_ORDERED_VERIFIER_AOT_ARENA_BYTES)) {
            ESP_LOGE(TAG, "Kizz ordered verifier AOT setup failed");
            this->mark_failed();
            return;
        }
        if (!run_streaming_aot_equivalence_self_test(
                "detector", detector_model_, &detector_aot_, 0x4b697a7au)) {
            ESP_LOGE(TAG,
                     "Kizz qualified detector AOT equivalence test failed");
            this->mark_failed();
            return;
        }
        if (!run_streaming_aot_equivalence_self_test(
                "ordered verifier", ordered_verifier_model_,
                &ordered_verifier_aot_, 0x4f726465u)) {
            ESP_LOGE(TAG, "Kizz ordered verifier AOT equivalence test failed");
            this->mark_failed();
            return;
        }
        reset_aot_detector();
        esphome::micro_wake_word::MicroWakeWord::setup();
        if (this->is_failed()) return;
        feature_ring_ = static_cast<int8_t *>(heap_caps_calloc(
            KIZZ_VERIFIER_VALUES, sizeof(int8_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        compact_verifier_aot_arena_ = static_cast<uint8_t *>(
            heap_caps_malloc(KIZZ_COMPACT_VERIFIER_AOT_ARENA_BYTES,
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!feature_ring_ || !compact_verifier_aot_arena_) {
            ESP_LOGE(TAG, "Kizz compact verifier allocation failed");
            this->mark_failed();
            return;
        }

        compact_verifier_model_ =
            tflite::GetModel(kizz_compact_verifier_model_start);
        if (!compact_verifier_model_ ||
            compact_verifier_model_->version() != TFLITE_SCHEMA_VERSION) {
            ESP_LOGE(TAG, "Kizz compact verifier schema is unsupported");
            this->mark_failed();
            return;
        }
        if (!compact_verifier_aot_.initialize(
                compact_verifier_model_, compact_verifier_aot_arena_,
                KIZZ_COMPACT_VERIFIER_AOT_ARENA_BYTES)) {
            ESP_LOGE(TAG, "Kizz compact verifier AOT setup failed");
            this->mark_failed();
            return;
        }
        if (!run_compact_aot_equivalence_self_test()) {
            ESP_LOGE(TAG, "Kizz compact verifier AOT equivalence test failed");
            this->mark_failed();
            return;
        }
        initialize_feature_requantization_luts();
        ESP_LOGI(TAG,
                 "Kizz compact verifier resident: execution=aot-fixed-c "
                 "arena_used=%u arena_capacity=%u input_scale=%.9f "
                 "memory=psram",
                 static_cast<unsigned>(
                     compact_verifier_aot_.arena_used_bytes()),
                 static_cast<unsigned>(KIZZ_COMPACT_VERIFIER_AOT_ARENA_BYTES),
                 static_cast<double>(
                     compact_verifier_aot_.input_scale()));
        ESP_LOGI(TAG,
                 "Kizz ordered verifier resident: execution=aot-fixed-c "
                 "arena_used=%u arena_capacity=%u memory=psram",
                 static_cast<unsigned>(
                     ordered_verifier_aot_.arena_used_bytes()),
                 static_cast<unsigned>(
                     KIZZ_ORDERED_VERIFIER_AOT_ARENA_BYTES));
    }

    void start() {
        reset_aot_detector();
        esphome::micro_wake_word::MicroWakeWord::start();
    }

    void stop() {
        reset_aot_detector();
        esphome::micro_wake_word::MicroWakeWord::stop();
    }

    void set_generic_ordered_state_model_threshold(
        float raw_score_threshold, float display_probability_cutoff) {
        if (!std::isfinite(raw_score_threshold)) return;
        detector_raw_threshold_ = raw_score_threshold;
        detector_display_cutoff_ = std::clamp(
            display_probability_cutoff, 0.001f, 0.999f);
        detector_decoder_.set_threshold(detector_raw_threshold_);
    }

    float get_wake_word_probability() const {
        if (!std::isfinite(detector_completion_score_)) return 0.0f;
        const float cutoff_logit = std::log(
            detector_display_cutoff_ / (1.0f - detector_display_cutoff_));
        const float shifted = detector_completion_score_ -
            detector_raw_threshold_ + cutoff_logit;
        if (shifted >= 40.0f) return 1.0f;
        if (shifted <= -40.0f) return 0.0f;
        return 1.0f / (1.0f + std::exp(-shifted));
    }

    bool process_detector_features(
        const int8_t features[
            esphome::micro_wake_word::PREPROCESSOR_FEATURE_SIZE]) {
        using esphome::micro_wake_word::OrderedStateStreamPrimer;
        if (!features || !detector_aot_.initialized()) return false;
        if (!detector_primer_.primed()) {
            int8_t primer[OrderedStateStreamPrimer::kStride]
                         [OrderedStateStreamPrimer::kFeatureSize];
            if (!detector_primer_.capture(
                    features, primer, detector_aot_.input_zero_point()))
                return false;
            uint8_t ignored[KizzDetectorAot::kOutputValues]{};
            if (!invoke_detector(&primer[0][0], ignored, true)) return false;
            detector_stride_step_ = 0;
            return false;
        }

        std::memcpy(
            detector_input_.data() +
                detector_stride_step_ * KizzDetectorAot::kFeatureBins,
            features, KizzDetectorAot::kFeatureBins);
        ++detector_stride_step_;
        if (detector_stride_step_ < KizzDetectorAot::kInputFrames)
            return false;
        detector_stride_step_ = 0;
        if (!invoke_detector(detector_input_.data(), detector_output_.data(),
                             false))
            return false;
        if (!detector_decoder_.step_quantized_uint8(
                detector_output_.data(), detector_aot_.output_scale(),
                detector_aot_.output_zero_point())) {
            ESP_LOGE(TAG, "Kizz AOT detector decoder rejected model output");
            return false;
        }
        const auto event = detector_decoder_.event();
        detector_completion_score_ = detector_decoder_.detected()
            ? event.score
            : detector_decoder_.completion_score();
        return detector_decoder_.detected();
    }

    void reset_aot_detector() {
        detector_aot_.reset();
        detector_primer_.reset();
        detector_decoder_.reset();
        detector_decoder_.set_threshold(detector_raw_threshold_);
        detector_decoder_.set_transition_probabilities(0.6f, 0.4f);
        detector_stride_step_ = 0;
        detector_completion_score_ = -INFINITY;
        detector_input_.fill(detector_aot_.input_zero_point());
        detector_output_.fill(0);
        candidate_pending_ = false;
        candidate_post_frames_ = 0;
    }

    bool candidate_pending() const { return candidate_pending_; }

    void begin_candidate(float probability) {
        candidate_pending_ = true;
        candidate_post_frames_ = 0;
        candidate_probability_ = probability;
    }

    bool finish_candidate_post_context(float *probability, float *logit,
                                       bool *accepted) {
        if (!candidate_pending_ || !probability || !logit || !accepted)
            return false;
        if (++candidate_post_frames_ < KIZZ_VERIFIER_POST_CONTEXT_FRAMES)
            return false;
        candidate_pending_ = false;
        *probability = candidate_probability_;
        *accepted = verify_candidate(logit);
        return true;
    }

    void accept_aot_detection() {
        reset_aot_detector();
        this->detected_wake_word_ = "Kizz Control detector";
        this->detected_ = true;
        this->set_state_(esphome::micro_wake_word::STOP_MICROPHONE);
    }

    void observe_features(
        const int8_t features[esphome::micro_wake_word::PREPROCESSOR_FEATURE_SIZE]) {
        if (!feature_ring_ || !features) return;
        int8_t *destination = feature_ring_ +
            feature_write_frame_ * KIZZ_VERIFIER_BINS;
        std::memcpy(destination, features, KIZZ_VERIFIER_BINS);
        feature_write_frame_ =
            (feature_write_frame_ + 1) % KIZZ_VERIFIER_FRAMES;
        valid_feature_frames_ = std::min(
            valid_feature_frames_ + 1, KIZZ_VERIFIER_FRAMES);
    }

    bool verify_candidate(float *logit) {
        const int64_t candidate_started_at = esp_timer_get_time();
        s_detector_candidates.fetch_add(1, std::memory_order_relaxed);
        s_verifier_runs.fetch_add(1, std::memory_order_relaxed);
        uint32_t model_invocations = 0;
        const auto finish_candidate = [&](bool success, bool accepted) {
            const uint32_t elapsed_us = static_cast<uint32_t>(
                esp_timer_get_time() - candidate_started_at);
            s_verifier_candidate_latency.record(elapsed_us);
            s_verifier_feature_frames.fetch_add(
                static_cast<uint32_t>(KIZZ_VERIFIER_FRAMES),
                std::memory_order_relaxed);
            update_max(s_verifier_max_feature_frames,
                       static_cast<uint32_t>(KIZZ_VERIFIER_FRAMES));
            s_verifier_model_invocations.fetch_add(
                model_invocations, std::memory_order_relaxed);
            update_max(s_verifier_max_model_invocations, model_invocations);
            if (!success) {
                s_verifier_errors.fetch_add(1, std::memory_order_relaxed);
                s_verifier_rejects.fetch_add(1, std::memory_order_relaxed);
            } else {
                s_verifier_full_windows.fetch_add(1,
                                                  std::memory_order_relaxed);
                if (accepted)
                    s_verifier_accepts.fetch_add(1,
                                                  std::memory_order_relaxed);
                else
                    s_verifier_rejects.fetch_add(1,
                                                  std::memory_order_relaxed);
            }
        };
        if (!logit || !feature_ring_ || !compact_verifier_aot_.initialized()) {
            finish_candidate(false, false);
            return false;
        }

        bool compact_accepted = false;
        if (!run_compact_verifier(logit, &compact_accepted,
                                  &model_invocations)) {
            finish_candidate(false, false);
            return false;
        }
        bool ordered_accepted = false;
        if (compact_accepted &&
            !run_ordered_verifier(logit, &ordered_accepted,
                                  &model_invocations)) {
            finish_candidate(false, false);
            return false;
        }
        const bool accepted = compact_accepted && ordered_accepted;
        finish_candidate(true, accepted);
        return accepted;
    }

    uint32_t detector_arena_used_bytes() const {
        return static_cast<uint32_t>(detector_aot_.arena_used_bytes());
    }

    uint32_t verifier_arena_used_bytes() const {
        return static_cast<uint32_t>(
            compact_verifier_aot_.arena_used_bytes() +
            ordered_verifier_aot_.arena_used_bytes());
    }

    uint32_t compact_verifier_arena_used_bytes() const {
        return static_cast<uint32_t>(
            compact_verifier_aot_.arena_used_bytes());
    }

    uint32_t ordered_verifier_arena_used_bytes() const {
        return static_cast<uint32_t>(
            ordered_verifier_aot_.arena_used_bytes());
    }

    bool detector_arena_in_psram() const {
        return detector_aot_arena_ &&
            esp_ptr_external_ram(detector_aot_arena_);
    }

    bool verifier_arena_in_psram() const {
        return compact_verifier_aot_arena_ &&
            esp_ptr_external_ram(compact_verifier_aot_arena_);
    }

 private:
    bool run_streaming_aot_equivalence_self_test(
        const char *label, const tflite::Model *model,
        KizzDetectorAot *executor, uint32_t random) {
        constexpr size_t kShadowArenaBytes = 48 * 1024;
        constexpr size_t kShadowVariableBytes = 1024;
        constexpr size_t kInvocations = 96;
        uint8_t *shadow_arena = static_cast<uint8_t *>(heap_caps_malloc(
            kShadowArenaBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        uint8_t *shadow_variables = static_cast<uint8_t *>(heap_caps_malloc(
            kShadowVariableBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!shadow_arena || !shadow_variables) {
            if (shadow_arena) heap_caps_free(shadow_arena);
            if (shadow_variables) heap_caps_free(shadow_variables);
            return false;
        }

        const bool passed = [&]() {
            tflite::MicroMutableOpResolver<12> resolver;
            if (resolver.AddCallOnce() != kTfLiteOk ||
                resolver.AddVarHandle() != kTfLiteOk ||
                resolver.AddReshape() != kTfLiteOk ||
                resolver.AddReadVariable() != kTfLiteOk ||
                resolver.AddConcatenation() != kTfLiteOk ||
                resolver.AddStridedSlice() != kTfLiteOk ||
                resolver.AddAssignVariable() != kTfLiteOk ||
                resolver.AddConv2D() != kTfLiteOk ||
                resolver.AddDepthwiseConv2D() != kTfLiteOk ||
                resolver.AddQuantize() != kTfLiteOk)
                return false;
            auto *allocator = tflite::MicroAllocator::Create(
                shadow_variables, kShadowVariableBytes);
            auto *resources = allocator
                ? tflite::MicroResourceVariables::Create(allocator, 20)
                : nullptr;
            if (!allocator || !resources) return false;
            auto interpreter = std::make_unique<tflite::MicroInterpreter>(
                model, resolver, shadow_arena, kShadowArenaBytes,
                resources);
            if (interpreter->AllocateTensors() != kTfLiteOk)
                return false;
            TfLiteTensor *input = interpreter->input(0);
            TfLiteTensor *output = interpreter->output(0);
            if (!input || input->type != kTfLiteInt8 ||
                input->bytes != KizzDetectorAot::kInputValues ||
                !output || output->type != kTfLiteUInt8 ||
                output->bytes != KizzDetectorAot::kOutputValues)
                return false;

            executor->reset();
            std::array<int8_t, KizzDetectorAot::kInputValues> values{};
            std::array<uint8_t, KizzDetectorAot::kOutputValues> actual{};
            uint64_t aot_total_us = 0;
            uint64_t tflm_total_us = 0;
            for (size_t call = 0; call < kInvocations; ++call) {
                for (size_t index = 0; index < values.size(); ++index) {
                    random ^= random << 13;
                    random ^= random >> 17;
                    random ^= random << 5;
                    values[index] = call == 0
                        ? static_cast<int8_t>(-128)
                        : call == 1
                            ? static_cast<int8_t>(127)
                            : static_cast<int8_t>(random & 0xff);
                }
                std::memcpy(input->data.int8, values.data(), values.size());
                int64_t started_at = esp_timer_get_time();
                if (!executor->invoke(values.data(), actual.data()))
                    return false;
                aot_total_us += esp_timer_get_time() - started_at;
                started_at = esp_timer_get_time();
                if (interpreter->Invoke() != kTfLiteOk) return false;
                tflm_total_us += esp_timer_get_time() - started_at;
                for (size_t channel = 0; channel < actual.size(); ++channel) {
                    if (actual[channel] == output->data.uint8[channel])
                        continue;
                    ESP_LOGE(TAG,
                             "Kizz %s AOT mismatch: call=%u channel=%u "
                             "aot=%u tflm=%u",
                             label,
                             static_cast<unsigned>(call),
                             static_cast<unsigned>(channel),
                             static_cast<unsigned>(actual[channel]),
                             static_cast<unsigned>(output->data.uint8[channel]));
                    return false;
                }
            }
            ESP_LOGI(TAG,
                     "Kizz %s AOT equivalence passed: invokes=%u outputs=%u "
                     "aot_total_us=%llu tflm_psram_total_us=%llu",
                     label,
                     static_cast<unsigned>(kInvocations),
                     static_cast<unsigned>(
                         kInvocations * KizzDetectorAot::kOutputValues),
                     static_cast<unsigned long long>(aot_total_us),
                     static_cast<unsigned long long>(tflm_total_us));
            return true;
        }();
        heap_caps_free(shadow_variables);
        heap_caps_free(shadow_arena);
        executor->reset();
        return passed;
    }

    bool run_compact_aot_equivalence_self_test() {
        constexpr size_t kInvocations = 4;
        // Generated with the exact embedded model using TensorFlow Lite's
        // BUILTIN_REF resolver. Espressif's TFLM 1.4 tail returns -128 for
        // this model after an otherwise matching convolution trunk, so the
        // deployed AOT graph is checked against model-bound golden outputs.
        constexpr std::array<int8_t, kInvocations> kExpectedOutputs{
            21, -97, -54, -118};
        uint64_t aot_total_us = 0;
        const bool passed = [&]() {
            uint32_t random = 0x436f6d70u;
            inference_cpu_boost_begin();
            for (size_t call = 0; call < kInvocations; ++call) {
                int8_t *values = compact_verifier_aot_.input_data();
                if (!values) return false;
                for (size_t index = 0;
                     index < KizzVerifierAot::kInputValues; ++index) {
                    random ^= random << 13;
                    random ^= random >> 17;
                    random ^= random << 5;
                    values[index] = call == 0
                        ? compact_verifier_aot_.input_zero_point()
                        : call == 1
                            ? static_cast<int8_t>(127)
                            : static_cast<int8_t>(random & 0xff);
                }
                float actual_logit = 0.0f;
                int64_t started_at = esp_timer_get_time();
                if (!compact_verifier_aot_.invoke(&actual_logit)) return false;
                aot_total_us += esp_timer_get_time() - started_at;
                const int32_t actual_quantized = static_cast<int32_t>(
                    std::lround(actual_logit /
                                compact_verifier_aot_.output_scale())) +
                    compact_verifier_aot_.output_zero_point();
                if (actual_quantized != kExpectedOutputs[call]) {
                    ESP_LOGE(TAG,
                             "Kizz compact AOT golden mismatch: call=%u "
                             "aot=%ld expected=%ld",
                             static_cast<unsigned>(call),
                             static_cast<long>(actual_quantized),
                             static_cast<long>(kExpectedOutputs[call]));
                    return false;
                }
                vTaskDelay(1);
            }
            inference_cpu_boost_end();
            return true;
        }();
        inference_cpu_boost_end();
        if (passed) {
            ESP_LOGI(TAG,
                     "Kizz compact AOT golden equivalence passed: "
                     "invokes=%u aot_total_us=%llu",
                     static_cast<unsigned>(kInvocations),
                     static_cast<unsigned long long>(aot_total_us));
        }
        return passed;
    }

    bool invoke_detector(const int8_t *input, uint8_t *output,
                         bool primer) {
        const auto end_event = primer
            ? esphome::micro_wake_word::PerformanceEvent::
                  ORDERED_PRIMER_INVOKE
            : esphome::micro_wake_word::PerformanceEvent::MODEL_INVOKE;
        inference_cpu_boost_begin();
        const int64_t started_at = esp_timer_get_time();
        const bool ok = detector_aot_.invoke(input, output);
        this->report_performance_(
            end_event, static_cast<uint32_t>(
                esp_timer_get_time() - started_at));
        inference_cpu_boost_end();
        if (!ok) ESP_LOGE(TAG, "Kizz AOT detector invoke failed");
        return ok;
    }

    int8_t source_feature(size_t frame, size_t bin) const {
        const size_t left_padding =
            KIZZ_VERIFIER_FRAMES - valid_feature_frames_;
        if (frame < left_padding) return -128;
        const size_t logical_frame = frame - left_padding;
        const size_t oldest_frame = valid_feature_frames_ ==
                KIZZ_VERIFIER_FRAMES
            ? feature_write_frame_
            : 0;
        const size_t source_frame =
            (oldest_frame + logical_frame) % KIZZ_VERIFIER_FRAMES;
        return feature_ring_[source_frame * KIZZ_VERIFIER_BINS + bin];
    }

    int8_t requantize_compact_feature(int8_t feature) const {
        return compact_feature_lut_[static_cast<uint8_t>(feature)];
    }

    int8_t requantize_ordered_feature(int8_t feature) const {
        return ordered_feature_lut_[static_cast<uint8_t>(feature)];
    }

    void initialize_feature_requantization_luts() {
        for (size_t raw = 0; raw < 256; ++raw) {
            const int8_t feature = static_cast<int8_t>(raw);
            const float real_value =
                (static_cast<int32_t>(feature) + 128) *
                KIZZ_FRONTEND_FEATURE_SCALE;
            const int32_t compact = static_cast<int32_t>(std::lround(
                real_value / compact_verifier_aot_.input_scale())) +
                compact_verifier_aot_.input_zero_point();
            compact_feature_lut_[raw] = static_cast<int8_t>(
                std::clamp<int32_t>(compact, -128, 127));
            const int32_t ordered = static_cast<int32_t>(std::lround(
                real_value / ordered_verifier_aot_.input_scale())) +
                ordered_verifier_aot_.input_zero_point();
            ordered_feature_lut_[raw] = static_cast<int8_t>(
                std::clamp<int32_t>(ordered, -128, 127));
        }
    }

    bool run_compact_verifier(float *score, bool *accepted,
                              uint32_t *model_invocations) {
        s_compact_verifier_runs.fetch_add(1, std::memory_order_relaxed);
        const int64_t started_at = esp_timer_get_time();
        int8_t *values = compact_verifier_aot_.input_data();
        if (!values || !score || !accepted || !model_invocations) {
            s_compact_verifier_errors.fetch_add(1,
                                                std::memory_order_relaxed);
            return false;
        }
        const int64_t frontend_started_at = esp_timer_get_time();
        for (size_t frame = 0; frame < KIZZ_VERIFIER_FRAMES; ++frame) {
            for (size_t bin = 0; bin < KIZZ_VERIFIER_BINS; ++bin) {
                values[frame * KIZZ_VERIFIER_BINS + bin] =
                    requantize_compact_feature(source_feature(frame, bin));
            }
        }
        s_verifier_frontend_latency.record(static_cast<uint32_t>(
            esp_timer_get_time() - frontend_started_at));

        inference_cpu_boost_begin();
        const int64_t invoke_started_at = esp_timer_get_time();
        const bool status = compact_verifier_aot_.invoke(score);
        const uint32_t invoke_us = static_cast<uint32_t>(
            esp_timer_get_time() - invoke_started_at);
        inference_cpu_boost_end();
        s_verifier_invoke_latency.record(invoke_us);
        ++*model_invocations;
        if (!status) {
            s_compact_verifier_errors.fetch_add(1,
                                                std::memory_order_relaxed);
            s_compact_verifier_latency.record(static_cast<uint32_t>(
                esp_timer_get_time() - started_at));
            return false;
        }
        *accepted = *score >= KIZZ_COMPACT_VERIFIER_RAW_SCORE_THRESHOLD;
        if (*accepted) {
            s_compact_verifier_accepts.fetch_add(1,
                                                 std::memory_order_relaxed);
        } else {
            s_compact_verifier_rejects.fetch_add(1,
                                                 std::memory_order_relaxed);
        }
        const uint32_t elapsed_us = static_cast<uint32_t>(
            esp_timer_get_time() - started_at);
        s_compact_verifier_latency.record(elapsed_us);
        ESP_LOGI(TAG,
                 "Kizz compact verifier: score=%.6f threshold=%.6f "
                 "pass=%s elapsed_us=%u",
                 static_cast<double>(*score),
                 static_cast<double>(KIZZ_COMPACT_VERIFIER_RAW_SCORE_THRESHOLD),
                 *accepted ? "true" : "false", elapsed_us);
        return true;
    }

    bool run_ordered_verifier(float *score, bool *accepted,
                              uint32_t *model_invocations) {
        if (!score || !accepted || !model_invocations ||
            !ordered_verifier_aot_.initialized())
            return false;
        s_ordered_verifier_runs.fetch_add(1, std::memory_order_relaxed);
        const int64_t started_at = esp_timer_get_time();
        ordered_verifier_aot_.reset();
        esphome::micro_wake_word::GenericOrderedStateDecoder decoder;
        decoder.set_threshold(KIZZ_ORDERED_VERIFIER_RAW_SCORE_THRESHOLD);
        decoder.set_transition_probabilities(0.6f, 0.4f);
        *accepted = false;
        *score = -INFINITY;
        uint32_t calls = 0;
        inference_cpu_boost_begin();
        for (size_t call = 0; call < KIZZ_ORDERED_VERIFIER_CALLS; ++call) {
            int8_t *values = ordered_verifier_input_.data();
            for (size_t row = 0; row < KIZZ_ORDERED_VERIFIER_STRIDE; ++row) {
                const bool primer_zero = call == 0 && row == 0;
                const size_t frame = call == 0
                    ? row - (primer_zero ? 0 : 1)
                    : KIZZ_ORDERED_VERIFIER_PHASE_OFFSET +
                        (call - 1) * KIZZ_ORDERED_VERIFIER_STRIDE + row;
                for (size_t bin = 0; bin < KIZZ_VERIFIER_BINS; ++bin) {
                    values[row * KIZZ_VERIFIER_BINS + bin] = primer_zero
                        ? ordered_verifier_aot_.input_zero_point()
                        : requantize_ordered_feature(
                              source_feature(frame, bin));
                }
            }
            const int64_t invoke_started_at = esp_timer_get_time();
            const bool status = ordered_verifier_aot_.invoke(
                values, ordered_verifier_output_.data());
            const uint32_t invoke_us = static_cast<uint32_t>(
                esp_timer_get_time() - invoke_started_at);
            s_verifier_invoke_latency.record(invoke_us);
            ++calls;
            ++*model_invocations;
            s_ordered_verifier_model_invocations.fetch_add(
                1, std::memory_order_relaxed);
            if (!status) {
                inference_cpu_boost_end();
                s_ordered_verifier_errors.fetch_add(
                    1, std::memory_order_relaxed);
                s_ordered_verifier_latency.record(static_cast<uint32_t>(
                    esp_timer_get_time() - started_at));
                return false;
            }
            if (call < KIZZ_ORDERED_VERIFIER_WARMUP_CALLS) continue;
            if (!decoder.step_quantized_uint8(
                    ordered_verifier_output_.data(),
                    ordered_verifier_aot_.output_scale(),
                    ordered_verifier_aot_.output_zero_point())) {
                inference_cpu_boost_end();
                s_ordered_verifier_errors.fetch_add(
                    1, std::memory_order_relaxed);
                return false;
            }
            *score = decoder.detected()
                ? decoder.event().score
                : std::max(*score, decoder.completion_score());
            if (decoder.detected()) {
                *accepted = true;
                if (calls < KIZZ_ORDERED_VERIFIER_CALLS)
                    s_verifier_early_exits.fetch_add(
                        1, std::memory_order_relaxed);
                break;
            }
        }
        inference_cpu_boost_end();
        if (*accepted)
            s_ordered_verifier_accepts.fetch_add(
                1, std::memory_order_relaxed);
        else
            s_ordered_verifier_rejects.fetch_add(
                1, std::memory_order_relaxed);
        const uint32_t elapsed_us = static_cast<uint32_t>(
            esp_timer_get_time() - started_at);
        s_ordered_verifier_latency.record(elapsed_us);
        ESP_LOGI(TAG,
                 "Kizz ordered verifier: score=%.6f threshold=%.6f "
                 "pass=%s calls=%u elapsed_us=%u",
                 static_cast<double>(*score),
                 static_cast<double>(KIZZ_ORDERED_VERIFIER_RAW_SCORE_THRESHOLD),
                 *accepted ? "true" : "false", calls, elapsed_us);
        return true;
    }

    const tflite::Model *detector_model_{nullptr};
    const tflite::Model *compact_verifier_model_{nullptr};
    const tflite::Model *ordered_verifier_model_{nullptr};
    KizzDetectorAot detector_aot_;
    KizzVerifierAot compact_verifier_aot_;
    KizzDetectorAot ordered_verifier_aot_;
    uint8_t *detector_aot_arena_{nullptr};
    uint8_t *ordered_verifier_aot_arena_{nullptr};
    uint8_t *compact_verifier_aot_arena_{nullptr};
    esphome::micro_wake_word::OrderedStateStreamPrimer detector_primer_{};
    esphome::micro_wake_word::GenericOrderedStateDecoder detector_decoder_{};
    std::array<int8_t, KizzDetectorAot::kInputValues> detector_input_{};
    std::array<uint8_t, KizzDetectorAot::kOutputValues> detector_output_{};
    std::array<int8_t, 256> compact_feature_lut_{};
    std::array<int8_t, 256> ordered_feature_lut_{};
    std::array<int8_t, KizzDetectorAot::kInputValues>
        ordered_verifier_input_{};
    std::array<uint8_t, KizzDetectorAot::kOutputValues>
        ordered_verifier_output_{};
    size_t detector_stride_step_{0};
    float detector_raw_threshold_{KIZZ_DETECTOR_RAW_SCORE_THRESHOLD};
    float detector_display_cutoff_{KIZZ_DEFAULT_PROBABILITY_CUTOFF};
    float detector_completion_score_{-INFINITY};
    bool candidate_pending_{false};
    size_t candidate_post_frames_{0};
    float candidate_probability_{0.0f};
    int8_t *feature_ring_{nullptr};
    size_t feature_write_frame_{0};
    size_t valid_feature_frames_{0};
};

esphome::microphone::ExternalAudioMicrophone *s_microphone = nullptr;
KizzMicroWakeWord *s_wake_word = nullptr;
kizz_wake_word_detected_cb_t s_detected_cb = nullptr;
kizz_wake_word_candidate_cb_t s_candidate_cb = nullptr;
TaskHandle_t s_detection_task = nullptr;
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
    // The detector and verifier remain resident across I2S handoffs. The
    // verifier is gated and consumes the same frontend feature history, so no
    // PCM replay or second frontend pass occurs on the candidate path.
    s_wake_word->set_retain_resources_on_stop(true);
    s_wake_word->set_generic_ordered_state_model_threshold(
        KIZZ_DETECTOR_RAW_SCORE_THRESHOLD, cutoff);
    s_wake_word->set_feature_callback(
        [](void *context, const int8_t *features) {
            auto *wake_word = static_cast<KizzMicroWakeWord *>(context);
            wake_word->observe_features(features);
            if (wake_word->candidate_pending()) {
                float probability = 0.0f;
                float logit = -INFINITY;
                bool accepted = false;
                if (!wake_word->finish_candidate_post_context(
                        &probability, &logit, &accepted))
                    return;
                if (s_candidate_cb) s_candidate_cb(accepted, logit);
                if (!accepted) {
                    wake_word->reset_aot_detector();
                    return;
                }
                s_detection_probability_milli =
                    probability_to_milli(probability);
                wake_word->accept_aot_detection();
                return;
            }
            if (!wake_word->process_detector_features(features) ||
                wake_word->ignore_windows_ < 0)
                return;
            const float probability = wake_word->get_wake_word_probability();
            wake_word->begin_candidate(probability);
        },
        s_wake_word);
    s_wake_word->set_performance_callback(
        detector_performance_callback, nullptr);
    s_wake_word->add_detection_callback([](std::string) {
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
             "Kizz cascade ready: detector=ordered-state-aot-fixed-c "
             "verifier=compact-dscnn-aot-esp-nn+ordered-state-aot "
             "raw_threshold=%.6f "
             "display_cutoff=%.2f compact_threshold=%.6f "
             "ordered_threshold=%.6f post_context_frames=%u "
             "detector_sha256=%s compact_sha256=%s ordered_sha256=%s",
             static_cast<double>(KIZZ_DETECTOR_RAW_SCORE_THRESHOLD),
             static_cast<double>(cutoff),
             static_cast<double>(KIZZ_COMPACT_VERIFIER_RAW_SCORE_THRESHOLD),
             static_cast<double>(KIZZ_ORDERED_VERIFIER_RAW_SCORE_THRESHOLD),
             static_cast<unsigned>(KIZZ_VERIFIER_POST_CONTEXT_FRAMES),
             KIZZ_DETECTOR_MODEL_SHA256,
             KIZZ_COMPACT_VERIFIER_MODEL_SHA256,
             KIZZ_ORDERED_VERIFIER_MODEL_SHA256);
    return true;
}

struct WakeSetupContext {
    SemaphoreHandle_t done{nullptr};
    bool result{false};
};

void wake_setup_task(void *argument) {
    auto *context = static_cast<WakeSetupContext *>(argument);
    context->result = create_wake_word_model();
    xSemaphoreGive(context->done);
    vTaskDelete(nullptr);
}

bool create_wake_word_model_with_setup_stack() {
    constexpr uint32_t KIZZ_SETUP_STACK_BYTES = 32 * 1024;
    constexpr TickType_t KIZZ_SETUP_TIMEOUT = pdMS_TO_TICKS(60000);
    WakeSetupContext context;
    context.done = xSemaphoreCreateBinary();
    if (!context.done) {
        ESP_LOGE(TAG, "Kizz setup completion semaphore allocation failed");
        return false;
    }
    TaskHandle_t setup_task = nullptr;
    const BaseType_t created = xTaskCreatePinnedToCoreWithCaps(
        wake_setup_task, "kizz_setup", KIZZ_SETUP_STACK_BYTES, &context, 6,
        &setup_task, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Kizz temporary setup task allocation failed");
        vSemaphoreDelete(context.done);
        return false;
    }
    if (xSemaphoreTake(context.done, KIZZ_SETUP_TIMEOUT) != pdTRUE) {
        ESP_LOGE(TAG, "Kizz model setup timed out after 60 seconds");
        vTaskDelete(setup_task);
        inference_cpu_boost_end();
        vSemaphoreDelete(context.done);
        return false;
    }
    vSemaphoreDelete(context.done);
    return context.result;
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
    for (;;) {
        if (s_reconfigure_requested.exchange(false)) {
            const float cutoff =
                s_probability_cutoff_milli.load() / 1000.0f;
            s_wake_word->set_generic_ordered_state_model_threshold(
                KIZZ_DETECTOR_RAW_SCORE_THRESHOLD, cutoff);
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
        if (now - s_perf_log_at_us >= 10000000) {
            s_perf_log_at_us = now;
            kizz_wake_word_log_performance();
        }
        // At 1 kHz this yields without sleeping long enough to starve the
        // official M5Unified microphone capture task on the same core.
        vTaskDelay(1);
    }
}
}  // namespace

extern "C" bool kizz_wake_word_reserve_fast_arena(void) {
    if (s_detector_fast_arena || s_wake_word) return true;
    const uint32_t free_before = static_cast<uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    const uint32_t largest_before = static_cast<uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    s_detector_fast_arena = static_cast<uint8_t *>(heap_caps_malloc(
        KIZZ_DETECTOR_TENSOR_ARENA_BYTES,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!s_detector_fast_arena) {
        ESP_LOGE(TAG,
                 "Kizz early detector arena reservation failed: bytes=%u "
                 "internal_free=%u largest=%u",
                 static_cast<unsigned>(KIZZ_DETECTOR_TENSOR_ARENA_BYTES),
                 static_cast<unsigned>(free_before),
                 static_cast<unsigned>(largest_before));
        return false;
    }
    ESP_LOGI(TAG,
             "Kizz detector fast arena reserved early: bytes=%u "
             "internal_free_before=%u largest_before=%u",
             static_cast<unsigned>(KIZZ_DETECTOR_TENSOR_ARENA_BYTES),
             static_cast<unsigned>(free_before),
             static_cast<unsigned>(largest_before));
    return true;
}

extern "C" bool kizz_wake_word_start(
    kizz_wake_word_detected_cb_t detected_cb,
    kizz_wake_word_candidate_cb_t candidate_cb) {
    if (s_wake_word) return true;
    // This adapter is fed exclusively by M5.Mic.record() in m5_platform.cpp;
    // it does not open or own a second physical microphone.
    s_microphone = new (std::nothrow)
        esphome::microphone::ExternalAudioMicrophone(
            KIZZ_AUDIO_QUEUE_CAPACITY_BYTES);
    if (!s_microphone || !s_microphone->is_ready()) {
        ESP_LOGE(TAG, "Kizz StackChan microphone adapter allocation failed");
        delete s_microphone;
        s_microphone = nullptr;
        return false;
    }
    s_detected_cb = detected_cb;
    s_candidate_cb = candidate_cb;
    s_runtime_target = WakeRuntimeTarget::ARMED;
    s_runtime_state = WakeRuntimeState::STARTING;
    s_perf_started_at_us = esp_timer_get_time();
    s_perf_log_at_us = s_perf_started_at_us;
    if (!s_inference_cpu_lock) {
        const esp_err_t lock_result = esp_pm_lock_create(
            ESP_PM_CPU_FREQ_MAX, 0, "kizz_nn", &s_inference_cpu_lock);
        if (lock_result == ESP_OK) {
            ESP_LOGI(TAG,
                     "Kizz 240 MHz boost ready (scoped to NN invokes)");
        } else {
            ESP_LOGW(TAG, "Kizz NN boost unavailable: %s",
                     esp_err_to_name(lock_result));
        }
    }
    load_persisted_config();
    if (!create_wake_word_model_with_setup_stack()) {
        ESP_LOGE(TAG, "Kizz Control student failed to start");
        delete s_microphone;
        s_microphone = nullptr;
        s_detected_cb = nullptr;
        s_candidate_cb = nullptr;
        return false;
    }
    if (xTaskCreatePinnedToCoreWithCaps(
            detection_task, "kizz_mww", 6144, nullptr, 6,
            &s_detection_task, 1,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(TAG, "Kizz Control wake task creation failed");
        s_wake_word->stop();
        delete s_wake_word;
        delete s_microphone;
        s_wake_word = nullptr;
        s_microphone = nullptr;
        s_detected_cb = nullptr;
        s_candidate_cb = nullptr;
        s_detection_task = nullptr;
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

extern "C" bool kizz_wake_word_get_performance(
    kizz_wake_word_perf_snapshot_t *snapshot) {
    if (!snapshot) return false;
    *snapshot = {};
    const int64_t now = esp_timer_get_time();
    snapshot->uptime_ms = s_perf_started_at_us > 0
        ? static_cast<uint64_t>(now - s_perf_started_at_us) / 1000
        : 0;
    snapshot->detector_hop_budget_us = KIZZ_DETECTOR_HOP_BUDGET_US;
    snapshot->audio_queue_capacity_bytes =
        KIZZ_AUDIO_QUEUE_CAPACITY_BYTES;
    snapshot->detector_hop = s_detector_hop_latency.summary();
    snapshot->detector_frontend = s_detector_frontend_latency.summary();
    snapshot->detector_invoke = s_detector_invoke_latency.summary();
    snapshot->verifier_candidate = s_verifier_candidate_latency.summary();
    snapshot->verifier_frontend = s_verifier_frontend_latency.summary();
    snapshot->verifier_invoke = s_verifier_invoke_latency.summary();
    snapshot->compact_verifier = s_compact_verifier_latency.summary();
    snapshot->ordered_verifier = s_ordered_verifier_latency.summary();
    snapshot->detector_hop_total_us = s_detector_hop_latency.total_us();
    snapshot->verifier_candidate_total_us =
        s_verifier_candidate_latency.total_us();
    const uint64_t uptime_us = snapshot->uptime_ms * 1000;
    if (uptime_us > 0) {
        snapshot->detector_compute_duty_ppm = static_cast<uint32_t>(
            static_cast<uint64_t>(snapshot->detector_hop_total_us) *
            1000000 / uptime_us);
        snapshot->verifier_compute_duty_ppm = static_cast<uint32_t>(
            static_cast<uint64_t>(snapshot->verifier_candidate_total_us) *
            1000000 / uptime_us);
        snapshot->cascade_compute_duty_ppm = static_cast<uint32_t>(
            std::min<uint64_t>(
                1000000,
                (static_cast<uint64_t>(snapshot->detector_hop_total_us) +
                 snapshot->verifier_candidate_total_us) * 1000000 /
                    uptime_us));
    }
    snapshot->detector_hops_over_80_percent_budget =
        s_detector_hops_over_80_percent_budget.load(
            std::memory_order_relaxed);
    snapshot->detector_hops_over_budget =
        s_detector_hops_over_budget.load(std::memory_order_relaxed);
    snapshot->detector_candidates =
        s_detector_candidates.load(std::memory_order_relaxed);
    snapshot->verifier_runs = s_verifier_runs.load(std::memory_order_relaxed);
    snapshot->verifier_accepts =
        s_verifier_accepts.load(std::memory_order_relaxed);
    snapshot->verifier_rejects =
        s_verifier_rejects.load(std::memory_order_relaxed);
    snapshot->verifier_errors =
        s_verifier_errors.load(std::memory_order_relaxed);
    snapshot->verifier_early_exits =
        s_verifier_early_exits.load(std::memory_order_relaxed);
    snapshot->verifier_full_windows =
        s_verifier_full_windows.load(std::memory_order_relaxed);
    snapshot->verifier_feature_frames =
        s_verifier_feature_frames.load(std::memory_order_relaxed);
    snapshot->verifier_model_invocations =
        s_verifier_model_invocations.load(std::memory_order_relaxed);
    snapshot->verifier_max_feature_frames =
        s_verifier_max_feature_frames.load(std::memory_order_relaxed);
    snapshot->verifier_max_model_invocations =
        s_verifier_max_model_invocations.load(std::memory_order_relaxed);
    snapshot->compact_verifier_runs =
        s_compact_verifier_runs.load(std::memory_order_relaxed);
    snapshot->compact_verifier_accepts =
        s_compact_verifier_accepts.load(std::memory_order_relaxed);
    snapshot->compact_verifier_rejects =
        s_compact_verifier_rejects.load(std::memory_order_relaxed);
    snapshot->compact_verifier_errors =
        s_compact_verifier_errors.load(std::memory_order_relaxed);
    snapshot->ordered_verifier_runs =
        s_ordered_verifier_runs.load(std::memory_order_relaxed);
    snapshot->ordered_verifier_accepts =
        s_ordered_verifier_accepts.load(std::memory_order_relaxed);
    snapshot->ordered_verifier_rejects =
        s_ordered_verifier_rejects.load(std::memory_order_relaxed);
    snapshot->ordered_verifier_errors =
        s_ordered_verifier_errors.load(std::memory_order_relaxed);
    snapshot->ordered_verifier_model_invocations =
        s_ordered_verifier_model_invocations.load(
            std::memory_order_relaxed);

    if (s_microphone) {
        const auto audio = s_microphone->stats();
        snapshot->audio_samples_offered = audio.offered_samples;
        snapshot->audio_samples_accepted = audio.accepted_samples;
        snapshot->audio_samples_dropped = audio.dropped_samples;
        snapshot->audio_queue_bytes = audio.queue_bytes;
        snapshot->audio_queue_high_water_bytes =
            audio.queue_high_water_bytes;
    }
    if (s_wake_word) {
        const auto runtime = s_wake_word->runtime_counters();
        snapshot->ring_buffer_overflow_resets =
            runtime.ring_buffer_overflow_resets;
        snapshot->partial_ring_writes = runtime.partial_ring_writes;
        snapshot->partial_feature_reads = runtime.partial_feature_reads;
        snapshot->detector_arena_used_bytes =
            s_wake_word->detector_arena_used_bytes();
        snapshot->verifier_arena_used_bytes =
            s_wake_word->verifier_arena_used_bytes();
        snapshot->compact_verifier_arena_used_bytes =
            s_wake_word->compact_verifier_arena_used_bytes();
        snapshot->ordered_verifier_arena_used_bytes =
            s_wake_word->ordered_verifier_arena_used_bytes();
        snapshot->detector_arena_in_psram =
            s_wake_word->detector_arena_in_psram();
        snapshot->verifier_arena_in_psram =
            s_wake_word->verifier_arena_in_psram();
    }

    snapshot->internal_heap_free_bytes =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    snapshot->internal_heap_min_free_bytes =
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    snapshot->internal_heap_largest_block_bytes =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    snapshot->psram_free_bytes =
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    snapshot->psram_min_free_bytes =
        heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    snapshot->psram_largest_block_bytes =
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    snapshot->detection_task_stack_min_free_bytes = s_detection_task
        ? static_cast<uint32_t>(uxTaskGetStackHighWaterMark(s_detection_task) *
                                sizeof(StackType_t))
        : 0;
    return true;
}

extern "C" void kizz_wake_word_log_performance(void) {
    kizz_wake_word_perf_snapshot_t perf = {};
    if (!kizz_wake_word_get_performance(&perf)) return;
    ESP_LOGI(TAG,
             "KIZZ_PERF timing uptime_ms=%llu "
             "detector_hop_us[n=%u]=%u/%u/%u/%u "
             "detector_frontend_us[n=%u]=%u/%u/%u/%u "
             "detector_invoke_us[n=%u]=%u/%u/%u/%u "
             "verifier_candidate_us[n=%u]=%u/%u/%u/%u "
             "compact_verifier_us[n=%u]=%u/%u/%u/%u "
             "ordered_verifier_us[n=%u]=%u/%u/%u/%u "
             "verifier_frontend_us[n=%u]=%u/%u/%u/%u "
             "verifier_invoke_us[n=%u]=%u/%u/%u/%u",
             static_cast<unsigned long long>(perf.uptime_ms),
             perf.detector_hop.samples,
             perf.detector_hop.p50_us, perf.detector_hop.p95_us,
             perf.detector_hop.p99_us, perf.detector_hop.max_us,
             perf.detector_frontend.samples,
             perf.detector_frontend.p50_us, perf.detector_frontend.p95_us,
             perf.detector_frontend.p99_us, perf.detector_frontend.max_us,
             perf.detector_invoke.samples,
             perf.detector_invoke.p50_us, perf.detector_invoke.p95_us,
             perf.detector_invoke.p99_us, perf.detector_invoke.max_us,
             perf.verifier_candidate.samples,
             perf.verifier_candidate.p50_us, perf.verifier_candidate.p95_us,
             perf.verifier_candidate.p99_us, perf.verifier_candidate.max_us,
             perf.compact_verifier.samples,
             perf.compact_verifier.p50_us, perf.compact_verifier.p95_us,
             perf.compact_verifier.p99_us, perf.compact_verifier.max_us,
             perf.ordered_verifier.samples,
             perf.ordered_verifier.p50_us, perf.ordered_verifier.p95_us,
             perf.ordered_verifier.p99_us, perf.ordered_verifier.max_us,
             perf.verifier_frontend.samples,
             perf.verifier_frontend.p50_us, perf.verifier_frontend.p95_us,
             perf.verifier_frontend.p99_us, perf.verifier_frontend.max_us,
             perf.verifier_invoke.samples,
             perf.verifier_invoke.p50_us, perf.verifier_invoke.p95_us,
             perf.verifier_invoke.p99_us, perf.verifier_invoke.max_us);
    ESP_LOGI(TAG,
             "KIZZ_PERF load hop_budget_us=%u hop_total_us=%u "
             "verifier_total_us=%u detector_duty_ppm=%u verifier_duty_ppm=%u "
             "cascade_duty_ppm=%u hops_over_80pct=%u hops_over_budget=%u "
             "detector_candidates=%u verifier_runs=%u "
             "accepts=%u rejects=%u errors=%u early_exits=%u full_windows=%u "
             "feature_frames=%u model_invocations=%u max_frames=%u "
             "max_invocations=%u compact=%u/%u/%u/%u "
             "ordered=%u/%u/%u/%u ordered_invocations=%u "
             "audio_offered=%u audio_accepted=%u "
             "audio_dropped=%u queue_bytes=%u queue_high_water_bytes=%u "
             "queue_capacity_bytes=%u "
             "ring_overflow_resets=%u partial_ring_writes=%u "
             "partial_feature_reads=%u",
             perf.detector_hop_budget_us, perf.detector_hop_total_us,
             perf.verifier_candidate_total_us,
             perf.detector_compute_duty_ppm,
             perf.verifier_compute_duty_ppm,
             perf.cascade_compute_duty_ppm,
             perf.detector_hops_over_80_percent_budget,
             perf.detector_hops_over_budget,
             perf.detector_candidates, perf.verifier_runs,
             perf.verifier_accepts, perf.verifier_rejects,
             perf.verifier_errors, perf.verifier_early_exits,
             perf.verifier_full_windows, perf.verifier_feature_frames,
             perf.verifier_model_invocations,
             perf.verifier_max_feature_frames,
             perf.verifier_max_model_invocations,
             perf.compact_verifier_runs, perf.compact_verifier_accepts,
             perf.compact_verifier_rejects, perf.compact_verifier_errors,
             perf.ordered_verifier_runs, perf.ordered_verifier_accepts,
             perf.ordered_verifier_rejects, perf.ordered_verifier_errors,
             perf.ordered_verifier_model_invocations,
             perf.audio_samples_offered, perf.audio_samples_accepted,
             perf.audio_samples_dropped, perf.audio_queue_bytes,
             perf.audio_queue_high_water_bytes,
             perf.audio_queue_capacity_bytes,
             perf.ring_buffer_overflow_resets, perf.partial_ring_writes,
             perf.partial_feature_reads);
    ESP_LOGI(TAG,
             "KIZZ_PERF memory detector_arena=%u detector_psram=%s "
             "verifier_arena=%u compact_arena=%u ordered_arena=%u "
             "verifier_psram=%s internal_free=%u "
             "internal_min=%u internal_largest=%u psram_free=%u "
             "psram_min=%u psram_largest=%u stack_min_free=%u",
             perf.detector_arena_used_bytes,
             perf.detector_arena_in_psram ? "true" : "false",
             perf.verifier_arena_used_bytes,
             perf.compact_verifier_arena_used_bytes,
             perf.ordered_verifier_arena_used_bytes,
             perf.verifier_arena_in_psram ? "true" : "false",
             perf.internal_heap_free_bytes, perf.internal_heap_min_free_bytes,
             perf.internal_heap_largest_block_bytes, perf.psram_free_bytes,
             perf.psram_min_free_bytes, perf.psram_largest_block_bytes,
             perf.detection_task_stack_min_free_bytes);
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
