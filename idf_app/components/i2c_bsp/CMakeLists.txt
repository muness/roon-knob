idf_component_register(
    SRCS "i2c_bsp.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2c esp_driver_gpio
)
