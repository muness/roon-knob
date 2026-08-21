idf_component_register(
    SRCS "kizz_wake_word.cpp"
    INCLUDE_DIRS "include"
    REQUIRES micro_wake_word
)

target_add_binary_data(${COMPONENT_LIB} "models/hiphi_kizz.tflite" BINARY)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD_REQUIRED ON)
