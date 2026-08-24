from pathlib import Path


ROOT = Path(__file__).parents[1]
PLATFORM = (ROOT / "components/m5_platform/m5_platform.cpp").read_text()
WAKE = (ROOT / "components/kizz_wake_word/kizz_wake_word.cpp").read_text()
HEADER = (ROOT / "components/kizz_wake_word/include/kizz_wake_word.h").read_text()


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
    capture = PLATFORM[PLATFORM.index("bool false_wake_start_capture()") :]
    assert "kizz_wake_word_detection_probability()" in capture
    assert "s_false_wake_probability = kizz_wake_word_probability()" not in capture
    assert '\\"pre_wake_ms\\":%u' in capture
    assert "FALSE_WAKE_PREROLL_MS" in capture
    assert 'c_pass ? "true" : "false", 1000' not in capture
    assert '\\"wake_to_finish_ms\\":%u' in capture
    assert '\\"dropped_busy_total\\":%u' in capture


def test_capture_gate_is_enrollment_specific_and_busy_loss_is_measured():
    assert "bool s_enrollment_transport_configured = false" in PLATFORM
    start_at = PLATFORM.index("bool false_wake_start_capture()")
    start = PLATFORM[
        start_at : PLATFORM.index("void false_wake_capture_audio", start_at)
    ]
    assert "s_enrollment_transport_configured" in start
    assert "s_voice_transport_configured" not in start.split("snprintf", 1)[0]
    assert "s_false_wake_dropped_busy.fetch_add(1)" in start
    assert "esp_websocket_client_is_connected" not in start
    callback_at = PLATFORM.index("if (!kizz_wake_word_start")
    callback = PLATFORM[callback_at : callback_at + 900]
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


if __name__ == "__main__":
    for test in (
        test_false_wake_buffer_contract_is_three_seconds_and_288000_bytes,
        test_metadata_uses_derived_preroll_and_exact_detection_score,
        test_detection_probability_is_a_callback_time_atomic_api,
        test_capture_gate_is_enrollment_specific_and_busy_loss_is_measured,
        test_enrollment_only_full_buffer_capture_rearms_the_detector,
    ):
        test()
    print("Kizz false-wake capture contract passed")
