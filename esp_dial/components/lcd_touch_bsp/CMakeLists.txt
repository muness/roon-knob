idf_component_register(
    SRCS "lcd_touch_bsp.c"
    INCLUDE_DIRS "."
    REQUIRES i2c_bsp
    PRIV_REQUIRES driver
)
