set(MWW_MANAGED_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../m5_beta_app/managed_components/micro_wake_word")

idf_component_register(
  SRCS
    "esphome/components/micro_wake_word/micro_wake_word.cpp"
    "esphome/components/micro_wake_word/streaming_model.cpp"
  INCLUDE_DIRS
    "include"
    "${MWW_MANAGED_ROOT}"
    "${MWW_MANAGED_ROOT}/include"
    "${MWW_MANAGED_ROOT}/include/esphome/core"
    "${MWW_MANAGED_ROOT}/include/esphome/components/micro_wake_word"
    "${MWW_MANAGED_ROOT}/include/esphome/components/microphone"
    "${MWW_MANAGED_ROOT}/include/esphome/components/i2s_audio"
    "${MWW_MANAGED_ROOT}/include/esphome/components/i2s_audio/microphone"
  REQUIRES
    "esp_common"
    "ESPMicroSpeechFeatures"
    "esp-tflite-micro"
    "esp_driver_i2s"
    "esp_system"
    "esp_timer"
    "freertos"
    "log"
)
