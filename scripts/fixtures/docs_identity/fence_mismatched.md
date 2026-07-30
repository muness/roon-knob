# A backtick fence cannot be closed by tildes

The block below opens with backticks and the author closed it with tildes. Markdown keeps the
backtick fence open, so everything after the tilde line is code as far as any Markdown reader is
concerned -- including the contradiction on the last line. The checker must agree and report an
unterminated fence rather than accepting the file.

```c
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x36, (uint8_t[]){0x00}, 1, 0},
};
~~~

The display is a round AMOLED with 16.7 million colours and no backlight control.
