set(SRC_FILES
    "first.c"
)

idf_component_register(
    SRC_DIRS hidden
    SRCS ${SRC_FILES}
)
