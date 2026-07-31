set(SRC_FILES
    "first.c"
)

list(APPEND SRC_FILES "hidden.c")

idf_component_register(
    SRCS ${SRC_FILES}
)
