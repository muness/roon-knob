set(m5_platform_requires m5stack__m5unified)
idf_build_get_property(m5_platform_target HIPHI_M5_TARGET)
if(m5_platform_target STREQUAL "dial" OR
   m5_platform_target STREQUAL "stackchan" OR
   m5_platform_target STREQUAL "atom_joystick")
    list(APPEND m5_platform_requires m5_official)
endif()

idf_component_register(
    SRCS "m5_platform.cpp" "m5_interaction_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES ${m5_platform_requires}
)

set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD_REQUIRED ON)
