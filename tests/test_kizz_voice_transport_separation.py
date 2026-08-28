from pathlib import Path


SOURCE = Path(__file__).parents[1] / "components/m5_platform/m5_platform.cpp"
source = SOURCE.read_text()
STACKCHAN_DEFAULTS = (
    Path(__file__).parents[1] / "m5_beta_app/sdkconfig.stackchan.defaults"
).read_text()


def function_body(signature: str, next_signature: str) -> str:
    start = source.index(signature)
    end = source.index(next_signature, start)
    return source[start:end]

# Production voice carries command audio only. Training samples and endpoint
# telemetry use the independently configured enrollment service.
assert '"wake_sample"' not in source
assert "voice_send_staged_wake_sample" not in source
assert "CONFIG_M5_PLATFORM_ENROLLMENT_WS_URI" in source
assert "enrollment_upload_audio_http" in source
assert "voice_telemetry" in source

# Recovery must not reset the AFE from the WebSocket callback while the fetch
# task is inside Espressif's VAD network. The fetch task continuously drains the
# AFE during response playback, so reopening the official M5Unified microphone
# does not require a cross-task reset.
take_microphone = function_body("bool voice_take_microphone()", "bool voice_take_speaker()")
assert "reset_buffer" not in take_microphone
assert "reset_vad" not in take_microphone

# A final commit follows a burst of streamed audio. Leave enough time for LAN
# socket backpressure to clear instead of dropping the whole turn at 1 second.
send_text = function_body("bool voice_send_text", "bool voice_send_start")
assert "pdMS_TO_TICKS(5000)" in send_text

# A clean server-side CLOSE must reconnect just like a transport error. A
# response deadline on the device prevents either service from leaving the
# microphone paused forever when the remote process wedges.
start_transport = function_body("void start_voice_transport()", "void stackchan_voice_note")
assert start_transport.count("enable_close_reconnect = true") == 2
fetch_task = function_body("void voice_fetch_task", "void voice_ws_event")
assert "Voice response timed out" in fetch_task
# A streaming STT/Codex result may beat the device's local VAD commit. The
# terminal marker and one ordered handoff prevent that late commit from putting
# the device back into a permanent response-wait state with an empty AFE.
response_wait = function_body("bool voice_begin_response_wait", "void voice_feed_task")
assert "VOICE_RESPONSE_TIMEOUT_US" in response_wait
assert "s_voice_waiting_for_response = true" in response_wait
assert "compare_exchange_strong" in response_wait
assert "VoiceTurnPhase::COMMITTING" in response_wait
assert "VoiceTurnPhase::TERMINAL" in response_wait
assert "voice_take_microphone()" in response_wait
assert "voice_take_speaker()" in response_wait
assert "s_voice_turn_phase.store(VoiceTurnPhase::CAPTURING" in fetch_task
assert fetch_task.count("skipped redundant") == 2
feedback = function_body(
    'extern "C" void m5_platform_voice_feedback',
    'extern "C" bool m5_platform_voice_is_listening',
)
assert "s_voice_turn_phase.exchange(" in feedback
assert "VoiceTurnPhase::TERMINAL" in feedback
# If a terminal result arrives while the fetch task is blocked sending audio,
# the callback must not restart feed() ahead of fetch(). Defer the physical
# microphone handoff to the fetch task after the blocking send returns.
assert "s_voice_rearm_pending.store(true" in feedback
assert "s_voice_rearm_pending.exchange(false" in fetch_task
flush_audio = function_body("size_t voice_flush_buffered_audio", "size_t voice_seed_recent_wake_audio")
assert "VoiceTurnPhase::TERMINAL" in flush_audio
upload_task = function_body("void enrollment_upload_task", "void enrollment_capture_audio")
assert "kizz_wake_word_resume()" in upload_task
assert 'enrollment_send_error(capture_id, "upload_failed")' in upload_task
assert "enrollment_upload_audio_http(" in upload_task
assert "esp_websocket_client_stop(s_enrollment_ws)" not in upload_task
assert "esp_websocket_client_start(s_enrollment_ws)" not in upload_task
http_upload = function_body("bool enrollment_upload_audio_http", "bool enrollment_valid_token")
assert "getaddrinfo" in http_upload
assert "SO_SNDTIMEO" in http_upload
assert "TCP_NODELAY" in http_upload
# The current uploader uses one ordinary HTTP connection per capture.
# Abortive close belonged to the retired one-socket-per-chunk path and can
# reset a completed response before lwIP has drained it.
assert "SO_LINGER" not in http_upload
assert "send_all" in http_upload
assert "DRAM_ATTR static char s_enrollment_http_io" in source
assert "DRAM_ATTR static uint8_t s_enrollment_http_chunk[4096]" in source
assert "send_all(fd, s_enrollment_http_io" in http_upload
assert "recv(fd, s_enrollment_http_io" in http_upload
assert "X-Device-ID: %s" in http_upload
assert "X-Detected: %s" in http_upload
assert "X-Audio-Offset: 0" in http_upload
assert "X-Audio-Total: %u" in http_upload
start_capture = function_body("bool enrollment_start_capture", "void enrollment_upload_task")
assert 'strncmp(upload_url, "http://", 7)' in start_capture
assert "!s_enrollment_upload_task" in start_capture
assert "xTaskCreatePinnedToCoreWithCaps" in start_transport
assert "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT" in start_transport
assert "constexpr size_t chunk_bytes = sizeof(s_enrollment_http_chunk)" in http_upload
assert "wake_dropped_since_log" in source
assert 'strcmp(type->valuestring, "training_chunk")' not in source
assert 'strcmp(type->valuestring, "stored")' not in source
assert "CONFIG_LWIP_MAX_SOCKETS=16" in STACKCHAN_DEFAULTS
assert "CONFIG_LWIP_TCP_MSL=5000" in STACKCHAN_DEFAULTS
assert "CONFIG_ESP_WIFI_STATIC_TX_BUFFER=y" in STACKCHAN_DEFAULTS
assert "CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM=2" in STACKCHAN_DEFAULTS

HTTP_SOURCE = (
    Path(__file__).parents[1] / "tough_app/main/platform_http_tough.c"
).read_text()
assert 'esp_http_client_set_header(client, "Connection", "close")' in HTTP_SOURCE
assert "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT" in HTTP_SOURCE

BRIDGE_SOURCE = (Path(__file__).parents[1] / "common/bridge_client.c").read_text()
assert "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT" in BRIDGE_SOURCE

print("Kizz production/enrollment transport separation passed")
