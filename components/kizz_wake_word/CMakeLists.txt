idf_component_register(
    SRCS "kizz_wake_word.cpp" "kizz_detector_aot.cpp" "kizz_verifier_aot.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_pm espressif__esp-nn micro_wake_word nvs_flash
)

target_add_binary_data(${COMPONENT_LIB} "models/kizz_control_detector.tflite" BINARY)
target_add_binary_data(${COMPONENT_LIB} "models/kizz_control_compact_verifier_int8_v10.tflite" BINARY)
target_add_binary_data(${COMPONENT_LIB} "models/kizz_control_ordered_verifier_int8.tflite" BINARY)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD_REQUIRED ON)
