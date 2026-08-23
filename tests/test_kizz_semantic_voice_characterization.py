from pathlib import Path


SOURCE = Path(__file__).parents[1] / "m5_beta_app/main/touch_ui.cpp"
source = SOURCE.read_text()
wake_source = (
    Path(__file__).parents[1]
    / "components/kizz_wake_word/kizz_wake_word.cpp"
).read_text()

apply_start = source.index('extern "C" void touch_ui_apply_semantic_family')
apply_end = source.index('extern "C" bool touch_ui_semantic_apply', apply_start)
apply = source[apply_start:apply_end]
assert "s_stackchan_marquees[3]" not in apply
assert "s_stackchan_marquees[4]" not in apply
assert "m5_platform_stackchan_sound" not in apply
assert "m5_platform_stackchan_expression" not in apply

diagnostics_start = source.index("void stackchan_draw_voice_diagnostics")
diagnostics_end = source.index("void stackchan_draw_thick_line", diagnostics_start)
diagnostics = source[diagnostics_start:diagnostics_end]
for marker in (
    "s.voice_transcript_until",
    "s.voice_response_until",
    "s_stackchan_marquees[3]",
    "s_stackchan_marquees[4]",
):
    assert marker in diagnostics

semantic_start = source.index("void render_semantic_family")
semantic_end = source.index("void render_stackchan_delight", semantic_start)
semantic = source[semantic_start:semantic_end]
assert "render_semantic_voice_overlay" in semantic
assert "KizzSemanticFamily::LISTENING_CONVERSATION" in semantic
assert "KizzSemanticFamily::REVIEW_CONFIRMATION" in semantic
assert "KizzSemanticFamily::STATUS_RECOVERY" in semantic

# Actionable visuals and process_input share the target-local native table.
assert "kizz_semantic_hit_region_for_action" in source
process_start = source.index("bool semantic_touch_input")
process_end = source.index("void process_input", process_start)
assert "kizz_semantic_hit_test" in source[process_start:process_end]

assert "if (!kizz_semantic_apply_changed()) return true;" in source

# ARMED must mean the detector is scheduled ahead of the always-on AFE fetch
# and can absorb short processing bursts without dropping a wake phrase.
assert "ExternalAudioMicrophone(32768)" in wake_source
assert 'detection_task, "kizz_mww", 6144, nullptr, 6' in wake_source
assert "vTaskDelay(1);" in wake_source
assert "vTaskDelay(pdMS_TO_TICKS(1));" not in wake_source

print("Kizz semantic voice lifecycle characterization passed")
