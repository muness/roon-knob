idf_component_register(
    SRCS "m5_platform.cpp" "m5_interaction_policy.c"
         "stackchan_ref/SCS.cpp" "stackchan_ref/SCSerial.cpp"
         "stackchan_ref/SCSCL.cpp"
    INCLUDE_DIRS "include" "stackchan_ref"
    REQUIRES m5stack__m5unified driver
)

set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD_REQUIRED ON)
