set(SRC_FILES
    "first.c"
)

idf_component_register(
    SRCS ${SRC_FILES} "hidden.c"
)
