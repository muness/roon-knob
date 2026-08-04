set(SRC_FILES
    "first.c"
)

idf_component_register(
    SRCS ${SRC_FILES}
)

set(HIDDEN_STEM hidden)
set(HIDDEN_EXT c)
set_property(
    TARGET ${COMPONENT_LIB} APPEND PROPERTY SOURCES
    "${HIDDEN_STEM}.${HIDDEN_EXT}"
)
