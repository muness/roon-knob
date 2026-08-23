from pathlib import Path


SOURCE = Path(__file__).parents[1] / "components/m5_platform/m5_platform.cpp"
source = SOURCE.read_text()


def function_body(signature: str, next_signature: str) -> str:
    start = source.index(signature)
    end = source.index(next_signature, start)
    return source[start:end]

# Production voice carries command audio only. Training samples and endpoint
# telemetry use the independently configured enrollment service.
assert '"wake_sample"' not in source
assert "voice_send_staged_wake_sample" not in source
assert "CONFIG_M5_PLATFORM_ENROLLMENT_WS_URI" in source
assert "enrollment_send_audio_chunks" in source
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
assert "VOICE_RESPONSE_TIMEOUT_US" in fetch_task
assert "Voice response timed out" in fetch_task
upload_task = function_body("void enrollment_upload_task", "void enrollment_capture_audio")
assert "kizz_wake_word_resume()" in upload_task
assert 'enrollment_send_error(capture_id, "upload_failed")' in upload_task
assert "Restarting stalled enrollment transport" in upload_task
assert "esp_websocket_client_stop(s_enrollment_ws)" in upload_task
assert "esp_websocket_client_start(s_enrollment_ws)" in upload_task
assert "CHUNK_BYTES = 4096" in source

print("Kizz production/enrollment transport separation passed")
