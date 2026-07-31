set(SRC_FILES
    "first.c"
)

idf_component_register(
    SRCS ${SRC_FILES}
)

target_sources(${COMPONENT_LIB} PRIVATE "hidden.c")
