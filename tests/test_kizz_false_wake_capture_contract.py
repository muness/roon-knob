from pathlib import Path


ROOT = Path(__file__).parents[1]
PLATFORM = (ROOT / "components/m5_platform/m5_platform.cpp").read_text()
WAKE = (ROOT / "components/kizz_wake_word/kizz_wake_word.cpp").read_text()
HEADER = (ROOT / "components/kizz_wake_word/include/kizz_wake_word.h").read_text()
WAKE_MANIFEST = (ROOT / "components/kizz_wake_word/idf_component.yml").read_text()
WAKE_CMAKE = (ROOT / "components/kizz_wake_word/CMakeLists.txt").read_text()
APP_MANIFEST = (ROOT / "m5_beta_app/main/idf_component.yml").read_text()
STACKCHAN_DEFAULTS = (ROOT / "m5_beta_app/sdkconfig.stackchan.defaults").read_text()
FORWARD_SUM_RUNTIME = "13581e0aacc2e73b3aa384b43463c953517cfe07"


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
        WAKE.index("s_wake_word->add_detection_callback") : WAKE.index(
            "    s_wake_word->setup();",
            WAKE.index("s_wake_word->add_detection_callback"),
        )
    ]
    assert "s_detection_probability_milli = probability_to_milli" in callback
    assert "get_wake_word_probability()" in callback
    assert "kizz_wake_word_detection_probability" in HEADER


def test_wake_pcm_comes_from_the_stackchan_m5unified_microphone():
    capture = PLATFORM[PLATFORM.index("void voice_feed_task") :]
    record_at = capture.index("M5.Mic.record(")
    feed_at = capture.index("kizz_wake_word_feed(")
    assert record_at < feed_at
    assert "ExternalAudioMicrophone" in WAKE
    assert "s_microphone->feed(samples, sample_count)" in WAKE


def test_scalar_detector_and_resident_verifier_are_the_runtime_decision():
    assert "add_wake_word_model(" in WAKE
    assert "_binary_hiphi_kizz_tflite_start" in WAKE
    assert "_binary_hiphi_kizz_device_specialist_tflite_start" in WAKE
    assert "verifier_.load_model(this->streaming_op_resolver_)" in WAKE
    assert "KIZZ_VERIFIER_EARLY_ACCEPT_CUTOFF" in WAKE
    assert "kizz_wake_word_verify_clip" in WAKE
    assert "kizz_wake_word_verify_clip" in HEADER
    assert "kizz_wake_word_verify_clip" in PLATFORM
    assert "add_ordered_state_model(" not in WAKE
    assert "set_ordered_state_model" not in WAKE
    assert "ordered" not in WAKE.lower()
    assert '"models/hiphi_kizz.tflite"' in WAKE_CMAKE
    assert '"models/hiphi_kizz_device_specialist.tflite"' in WAKE_CMAKE
    assert '"models/hiphi_kizz_ordered.tflite"' not in WAKE_CMAKE
    assert 'wake_model", "kizz_control_compact_ctc_v1' in PLATFORM


def test_external_forward_sum_runtime_cannot_be_shadowed_by_local_component():
    assert FORWARD_SUM_RUNTIME in WAKE_MANIFEST
    assert FORWARD_SUM_RUNTIME in APP_MANIFEST
    assert not (ROOT / "components/micro_wake_word/CMakeLists.txt").exists()


def test_detector_holds_full_cpu_only_while_wake_detection_is_armed():
    assert "ESP_PM_CPU_FREQ_MAX" in WAKE
    assert "needs_full_cpu = target == WakeRuntimeTarget::ARMED" in WAKE
    assert "esp_pm_lock_acquire(cpu_frequency_lock)" in WAKE
    assert "esp_pm_lock_release(cpu_frequency_lock)" in WAKE
    assert "verifier_.load_model(this->streaming_op_resolver_)" in WAKE


def test_stackchan_profile_pins_hardware_proven_audio_resource_choices():
    assert "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y" in STACKCHAN_DEFAULTS
    assert "CONFIG_SR_VADN_WEBRTC=y" in STACKCHAN_DEFAULTS
    assert "CONFIG_SR_WN_WN9_MYCROFT_TTS=y" in STACKCHAN_DEFAULTS


if __name__ == "__main__":
    for test in (
        test_false_wake_buffer_contract_is_three_seconds_and_288000_bytes,
        test_metadata_uses_derived_preroll_and_exact_detection_score,
        test_detection_probability_is_a_callback_time_atomic_api,
        test_wake_pcm_comes_from_the_stackchan_m5unified_microphone,
        test_scalar_detector_and_resident_verifier_are_the_runtime_decision,
        test_external_forward_sum_runtime_cannot_be_shadowed_by_local_component,
        test_detector_holds_full_cpu_only_while_wake_detection_is_armed,
        test_stackchan_profile_pins_hardware_proven_audio_resource_choices,
        test_capture_gate_is_enrollment_specific_and_busy_loss_is_measured,
        test_enrollment_only_full_buffer_capture_rearms_the_detector,
    ):
        test()
    print("Kizz false-wake capture contract passed")
