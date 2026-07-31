set(SRC_FILES
    "first.c"
)

idf_component_register(
    SRCS ${SRC_FILES}
)

include(extra_sources.cmake)
