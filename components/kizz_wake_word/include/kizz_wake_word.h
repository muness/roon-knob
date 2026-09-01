#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*kizz_wake_word_detected_cb_t)(void);
typedef void (*kizz_wake_word_candidate_cb_t)(bool accepted, float logit);

typedef struct {
    uint32_t samples;
    uint32_t p50_us;
    uint32_t p95_us;
    uint32_t p99_us;
    uint32_t max_us;
} kizz_latency_summary_t;

typedef struct {
    uint64_t uptime_ms;
    uint32_t detector_hop_budget_us;
    uint32_t audio_queue_capacity_bytes;
    kizz_latency_summary_t detector_hop;
    kizz_latency_summary_t detector_frontend;
    kizz_latency_summary_t detector_invoke;
    kizz_latency_summary_t verifier_candidate;
    kizz_latency_summary_t verifier_frontend;
    kizz_latency_summary_t verifier_invoke;
    kizz_latency_summary_t compact_verifier;
    kizz_latency_summary_t ordered_verifier;
    uint32_t detector_hop_total_us;
    uint32_t verifier_candidate_total_us;
    uint32_t detector_compute_duty_ppm;
    uint32_t verifier_compute_duty_ppm;
    uint32_t cascade_compute_duty_ppm;
    uint32_t detector_hops_over_80_percent_budget;
    uint32_t detector_hops_over_budget;
    uint32_t detector_candidates;
    uint32_t verifier_runs;
    uint32_t verifier_accepts;
    uint32_t verifier_rejects;
    uint32_t verifier_errors;
    uint32_t verifier_early_exits;
    uint32_t verifier_full_windows;
    uint32_t verifier_feature_frames;
    uint32_t verifier_model_invocations;
    uint32_t verifier_max_feature_frames;
    uint32_t verifier_max_model_invocations;
    uint32_t compact_verifier_runs;
    uint32_t compact_verifier_accepts;
    uint32_t compact_verifier_rejects;
    uint32_t compact_verifier_errors;
    uint32_t ordered_verifier_runs;
    uint32_t ordered_verifier_accepts;
    uint32_t ordered_verifier_rejects;
    uint32_t ordered_verifier_errors;
    uint32_t ordered_verifier_model_invocations;
    uint32_t audio_samples_offered;
    uint32_t audio_samples_accepted;
    uint32_t audio_samples_dropped;
    uint32_t audio_queue_bytes;
    uint32_t audio_queue_high_water_bytes;
    uint32_t ring_buffer_overflow_resets;
    uint32_t partial_ring_writes;
    uint32_t partial_feature_reads;
    uint32_t detector_arena_used_bytes;
    uint32_t verifier_arena_used_bytes;
    uint32_t compact_verifier_arena_used_bytes;
    uint32_t ordered_verifier_arena_used_bytes;
    bool detector_arena_in_psram;
    bool verifier_arena_in_psram;
    uint32_t internal_heap_free_bytes;
    uint32_t internal_heap_min_free_bytes;
    uint32_t internal_heap_largest_block_bytes;
    uint32_t psram_free_bytes;
    uint32_t psram_min_free_bytes;
    uint32_t psram_largest_block_bytes;
    uint32_t detection_task_stack_min_free_bytes;
} kizz_wake_word_perf_snapshot_t;

bool kizz_wake_word_reserve_fast_arena(void);
bool kizz_wake_word_start(kizz_wake_word_detected_cb_t detected_cb,
                          kizz_wake_word_candidate_cb_t candidate_cb);
size_t kizz_wake_word_feed(const int16_t *samples, size_t sample_count);
void kizz_wake_word_pause(void);
void kizz_wake_word_resume(void);
const char *kizz_wake_word_runtime_state(void);
uint32_t kizz_wake_word_transition_count(void);
float kizz_wake_word_probability(void);
// Probability captured at the exact detector callback that caused the wake.
// Unlike kizz_wake_word_probability(), this is not the later telemetry peak.
float kizz_wake_word_detection_probability(void);
bool kizz_wake_word_get_performance(
    kizz_wake_word_perf_snapshot_t *snapshot);
void kizz_wake_word_log_performance(void);
bool kizz_wake_word_configure(float probability_cutoff,
                              size_t sliding_window);
void kizz_wake_word_get_config(float *probability_cutoff,
                               size_t *sliding_window);

#ifdef __cplusplus
}
#endif
