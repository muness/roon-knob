set(m5_official_sources)
set(m5_official_includes)
set(m5_official_requires m5stack__m5unified espressif__arduino-esp32)
set(m5_official_vendor
    "${CMAKE_CURRENT_LIST_DIR}/../../components/m5_official/vendor")
idf_build_get_property(m5_official_target HIPHI_M5_TARGET)

# M5Stack publishes the board-specific libraries below as Arduino libraries.
# Keep each immutable upstream tree unmodified and compile only the library for
# the exact qualified target. Common display, touch, button, IMU, and power
# support continues to come from the managed M5Unified/M5GFX components.
if(m5_official_target STREQUAL "dial")
    list(APPEND m5_official_sources
        "${m5_official_vendor}/M5Dial/src/M5Dial.cpp"
        "${m5_official_vendor}/M5Dial/src/utility/Encoder.cpp"
        "${m5_official_vendor}/M5Dial/src/utility/MFRC522.cpp"
    )
    list(APPEND m5_official_includes
        "${m5_official_vendor}/M5Dial/src")
elseif(m5_official_target STREQUAL "stackchan")
    file(GLOB_RECURSE m5_official_sources
        "${m5_official_vendor}/StackChan-BSP/src/*.cpp"
    )
    list(APPEND m5_official_includes
        "${m5_official_vendor}/StackChan-BSP/src")
    list(APPEND m5_official_requires nvs_flash driver)
elseif(m5_official_target STREQUAL "atom_joystick")
    list(APPEND m5_official_sources
        "${m5_official_vendor}/Atom-JoyStick/src/AtomJoyStick.cpp"
    )
    list(APPEND m5_official_includes
        "${m5_official_vendor}/Atom-JoyStick/src")
else()
    message(FATAL_ERROR
        "m5_official is only valid for dial, stackchan, or atom_joystick")
endif()

idf_component_register(
    SRCS ${m5_official_sources}
    INCLUDE_DIRS ${m5_official_includes}
    REQUIRES ${m5_official_requires}
)

# Arduino's public compile options reach the board library and its consumers,
# but not the managed M5Unified/M5GFX libraries themselves. Both public header
# surfaces contain Arduino-conditional types, so every translation unit in the
# abstraction stack must agree on their layout. Compile both managed components
# in the same mode as the official board libraries to avoid mixed-ABI objects at
# M5.begin(config_t) and M5Unified::addDisplay(M5GFX&).
idf_component_get_property(m5_unified_lib m5stack__m5unified COMPONENT_LIB)
idf_component_get_property(m5_gfx_lib m5stack__m5gfx COMPONENT_LIB)
idf_component_get_property(arduino_lib espressif__arduino-esp32 COMPONENT_LIB)
target_link_libraries(${m5_unified_lib} PRIVATE ${arduino_lib})
target_link_libraries(${m5_gfx_lib} PRIVATE ${arduino_lib})

set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD_REQUIRED ON)

if(m5_official_target STREQUAL "atom_joystick")
    # Atom-JoyStick V0.0.1 leaves its local ADC result uninitialized for enum
    # values outside the two public modes. Keep the pinned upstream source
    # immutable while allowing GCC 14 to compile the official library.
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wno-error=maybe-uninitialized)
endif()
