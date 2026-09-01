import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).parents[1]
PLATFORM = (ROOT / "components/m5_platform/m5_platform.cpp").read_text()
WAKE = (ROOT / "components/kizz_wake_word/kizz_wake_word.cpp").read_text()
HEADER = (ROOT / "components/kizz_wake_word/include/kizz_wake_word.h").read_text()
WAKE_MANIFEST = (ROOT / "components/kizz_wake_word/idf_component.yml").read_text()
WAKE_CMAKE = (ROOT / "components/kizz_wake_word/CMakeLists.txt").read_text()
APP_MANIFEST = (ROOT / "m5_beta_app/main/idf_component.yml").read_text()
STACKCHAN_DEFAULTS = (ROOT / "m5_beta_app/sdkconfig.stackchan.defaults").read_text()
CAPTIVE_PORTAL = (ROOT / "tough_app/main/captive_portal.c").read_text()
PROVENANCE_PATH = (
    ROOT
    / "components/kizz_wake_word/models/kizz_control_cascade_v10.provenance.json"
)
AOT_RUNTIME = "cc19120f2a363c14a7b1fef9850380abdcbe422f"


def test_false_wake_buffer_contract_is_three_seconds_and_288000_bytes():
    assert "FALSE_WAKE_SAMPLE_RATE_HZ = 16000" in PLATFORM
    assert "FALSE_WAKE_PREROLL_SECONDS = 3" in PLATFORM
    assert "FALSE_WAKE_CAPTURE_SECONDS = 9" in PLATFORM
    assert "FALSE_WAKE_SAMPLE_RATE_HZ * FALSE_WAKE_PREROLL_SECONDS" in PLATFORM
    assert "FALSE_WAKE_SAMPLE_RATE_HZ * FALSE_WAKE_CAPTURE_SECONDS" in PLATFORM
    assert "FALSE_WAKE_MAX_SAMPLES * sizeof(int16_t)" in PLATFORM
    assert "static_assert(FALSE_WAKE_MAX_BYTES == 288000" in PLATFORM
    assert "static_assert(FALSE_WAKE_PREROLL_MS == 3000" in PLATFORM


def test_metadata_uses_derived_preroll_and_exact_detection_score():
    capture = PLATFORM[PLATFORM.index("bool false_wake_start_capture(") :]
    assert "kizz_wake_word_detection_probability()" in capture
    assert "s_false_wake_probability = kizz_wake_word_probability()" not in capture
    assert '\\"pre_wake_ms\\":%u' in capture
    assert "FALSE_WAKE_PREROLL_MS" in capture
    assert 'c_pass ? "true" : "false", 1000' not in capture
    assert '\\"wake_to_finish_ms\\":%u' in capture
    assert '\\"dropped_busy_total\\":%u' in capture


def test_capture_gate_is_enrollment_specific_and_busy_loss_is_measured():
    assert "bool s_enrollment_transport_configured = false" in PLATFORM
    start_at = PLATFORM.index("bool false_wake_start_capture(")
    start = PLATFORM[
        start_at : PLATFORM.index("void false_wake_capture_audio", start_at)
    ]
    assert "s_enrollment_transport_configured" in start
    assert "s_voice_transport_configured" not in start.split("snprintf", 1)[0]
    assert "s_false_wake_dropped_busy.fetch_add(1)" in start
    assert "esp_websocket_client_is_connected" not in start
    callback_at = PLATFORM.index("if (!kizz_wake_word_start")
    callback = PLATFORM[
        callback_at : PLATFORM.index("        }))", callback_at)
    ]
    assert "const bool evidence_started" in callback
    assert "if (s_voice_transport_configured)" in callback
    assert "else if (!evidence_started)" in callback
    assert "s_false_wake_samples >= FALSE_WAKE_MAX_SAMPLES" in PLATFORM


def test_enrollment_only_full_buffer_capture_rearms_the_detector():
    capture_at = PLATFORM.index("void false_wake_capture_audio")
    capture = PLATFORM[
        capture_at : PLATFORM.index("void false_wake_finish_capture", capture_at)
    ]
    assert "if (!s_voice_transport_configured) kizz_wake_word_resume();" in capture


def test_detection_probability_is_a_callback_time_atomic_api():
    assert "std::atomic<uint16_t> s_detection_probability_milli" in WAKE
    callback = WAKE[
        WAKE.index("s_wake_word->set_feature_callback") : WAKE.index(
            "    s_wake_word->set_performance_callback(",
            WAKE.index("s_wake_word->set_feature_callback"),
        )
    ]
    assert "s_detection_probability_milli =" in callback
    assert "probability_to_milli(probability)" in callback
    assert "get_wake_word_probability()" in callback
    assert "if (!accepted)" in callback
    assert "accept_aot_detection()" in callback
    assert "kizz_wake_word_detection_probability" in HEADER


def test_wake_pcm_comes_from_the_stackchan_m5unified_microphone():
    capture = PLATFORM[PLATFORM.index("void voice_feed_task") :]
    record_at = capture.index("M5.Mic.record(")
    feed_at = capture.index("kizz_wake_word_feed(")
    assert record_at < feed_at
    assert "ExternalAudioMicrophone" in WAKE
    assert "s_microphone->feed(samples, sample_count)" in WAKE


def test_three_stage_aot_cascade_is_the_runtime_decision():
    assert "_binary_kizz_control_detector_tflite_start" in WAKE
    assert "_binary_kizz_control_compact_verifier_int8_v10_tflite_start" in WAKE
    assert "_binary_kizz_control_ordered_verifier_int8_tflite_start" in WAKE
    assert "KizzDetectorAot detector_aot_" in WAKE
    assert "KizzVerifierAot compact_verifier_aot_" in WAKE
    assert "KizzDetectorAot ordered_verifier_aot_" in WAKE
    assert "const bool accepted = compact_accepted && ordered_accepted" in WAKE
    assert "if (compact_accepted &&" in WAKE
    assert "KIZZ_VERIFIER_POST_CONTEXT_FRAMES = 39" in WAKE
    assert "KIZZ_VERIFIER_FRAMES = 260" in WAKE
    assert "set_feature_callback(" in WAKE
    assert "begin_candidate(probability)" in WAKE
    assert "finish_candidate_post_context(" in WAKE
    assert '"models/kizz_control_detector.tflite"' in WAKE_CMAKE
    assert '"models/kizz_control_compact_verifier_int8_v10.tflite"' in WAKE_CMAKE
    assert '"models/kizz_control_ordered_verifier_int8.tflite"' in WAKE_CMAKE
    assert 'wake_model", "kizz_control_compact_ctc_v1' in PLATFORM


def test_external_forward_sum_runtime_cannot_be_shadowed_by_local_component():
    assert AOT_RUNTIME in WAKE_MANIFEST
    assert AOT_RUNTIME in APP_MANIFEST
    assert not (ROOT / "components/micro_wake_word/CMakeLists.txt").exists()


def test_stackchan_cascade_exposes_hardware_performance_qualification_metrics():
    assert "KIZZ_DETECTOR_HOP_BUDGET_US = 10000" in WAKE
    assert "KIZZ_AUDIO_QUEUE_CAPACITY_BYTES = 16384" in WAKE
    assert "PerformanceEvent::PIPELINE_HOP" in WAKE
    assert "detector_hops_over_80_percent_budget" in HEADER
    assert "detector_hops_over_budget" in HEADER
    assert "cascade_compute_duty_ppm" in HEADER
    assert "audio_samples_dropped" in HEADER
    assert "audio_queue_high_water_bytes" in HEADER
    assert "ring_buffer_overflow_resets" in HEADER
    assert "detector_arena_used_bytes" in HEADER
    assert "verifier_arena_used_bytes" in HEADER
    assert "detection_task_stack_min_free_bytes" in HEADER
    assert "KIZZ_PERF timing" in WAKE
    assert "KIZZ_PERF load" in WAKE
    assert "KIZZ_PERF memory" in WAKE
    assert "kizz_wake_word_candidate_cb_t" in HEADER
    assert "s_candidate_cb(accepted, logit)" in WAKE
    assert "Kizz detector candidate rejected by layered cascade" in PLATFORM


def test_cpu_boost_is_scoped_to_nn_invocations_not_the_armed_lifetime():
    assert "ESP_PM_CPU_FREQ_MAX" in WAKE
    assert '"Kizz 240 MHz boost ready (scoped to NN invokes)"' in WAKE
    assert "esp_pm_lock_acquire(s_inference_cpu_lock)" in WAKE
    assert "esp_pm_lock_release(s_inference_cpu_lock)" in WAKE
    detector_invoke = WAKE[WAKE.index("bool invoke_detector(") :]
    assert detector_invoke.index("inference_cpu_boost_begin()") < detector_invoke.index(
        "detector_aot_.invoke"
    )
    assert detector_invoke.index("detector_aot_.invoke") < detector_invoke.index(
        "inference_cpu_boost_end()"
    )
    ordered = WAKE[WAKE.index("bool run_ordered_verifier(") :]
    assert ordered.index("inference_cpu_boost_begin()") < ordered.index(
        "for (size_t call = 0; call < KIZZ_ORDERED_VERIFIER_CALLS; ++call)"
    )
    assert "needs_full_cpu = target == WakeRuntimeTarget::ARMED" not in WAKE


def test_boot_fails_closed_if_any_aot_executor_disagrees_with_reference():
    assert 'run_streaming_aot_equivalence_self_test(\n                "detector"' in WAKE
    assert 'run_streaming_aot_equivalence_self_test(\n                "ordered verifier"' in WAKE
    assert "run_compact_aot_equivalence_self_test()" in WAKE
    assert "tensorflow_lite_builtin_ref" in PROVENANCE_PATH.read_text()
    assert "Kizz compact AOT golden mismatch" in WAKE
    assert "Kizz qualified detector AOT equivalence test failed" in WAKE
    assert "Kizz ordered verifier AOT equivalence test failed" in WAKE
    assert "Kizz compact verifier AOT equivalence test failed" in WAKE
    assert "if (s_wake_word->is_failed())" in WAKE


def test_embedded_models_match_the_accepted_cascade_provenance():
    provenance = json.loads(PROVENANCE_PATH.read_text())
    model_root = PROVENANCE_PATH.parent
    for model in provenance["models"].values():
        model_path = model_root / model["filename"]
        assert model_path.is_file()
        assert hashlib.sha256(model_path.read_bytes()).hexdigest() == model["sha256"]
        assert model["sha256"] in WAKE
    assert provenance["phrase"] == "Kizz Control"
    assert provenance["physical_hardware_evaluation"]["speaker_replay_accepts"] == 12
    assert provenance["physical_hardware_evaluation"]["speaker_replay_attempts"] == 12


def test_stackchan_profile_pins_hardware_proven_audio_resource_choices():
    assert "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y" in STACKCHAN_DEFAULTS
    assert "CONFIG_SR_VADN_WEBRTC=y" in STACKCHAN_DEFAULTS
    assert "CONFIG_SR_WN_WN9_MYCROFT_TTS=y" in STACKCHAN_DEFAULTS
    assert "CONFIG_MDNS_TASK_CREATE_FROM_SPIRAM=y" in STACKCHAN_DEFAULTS
    assert "config.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;" in CAPTIVE_PORTAL


if __name__ == "__main__":
    for test in (
        test_false_wake_buffer_contract_is_three_seconds_and_288000_bytes,
        test_metadata_uses_derived_preroll_and_exact_detection_score,
        test_detection_probability_is_a_callback_time_atomic_api,
        test_wake_pcm_comes_from_the_stackchan_m5unified_microphone,
        test_three_stage_aot_cascade_is_the_runtime_decision,
        test_external_forward_sum_runtime_cannot_be_shadowed_by_local_component,
        test_stackchan_cascade_exposes_hardware_performance_qualification_metrics,
        test_cpu_boost_is_scoped_to_nn_invocations_not_the_armed_lifetime,
        test_boot_fails_closed_if_any_aot_executor_disagrees_with_reference,
        test_embedded_models_match_the_accepted_cascade_provenance,
        test_stackchan_profile_pins_hardware_proven_audio_resource_choices,
        test_capture_gate_is_enrollment_specific_and_busy_loss_is_measured,
        test_enrollment_only_full_buffer_capture_rearms_the_detector,
    ):
        test()
    print("Kizz false-wake capture contract passed")
