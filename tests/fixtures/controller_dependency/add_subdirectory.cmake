set(SRC_FILES
    "first.c"
)

idf_component_register(
    SRCS ${SRC_FILES}
)

add_subdirectory(extra)
