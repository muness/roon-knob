idf_component_register(
    SRCS "m5_platform.cpp" "m5_interaction_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES m5stack__m5unified driver
)

set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD_REQUIRED ON)
