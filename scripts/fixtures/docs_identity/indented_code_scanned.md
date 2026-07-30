# An indented block is scanned as prose

The guarded wording below is indented four spaces, which makes it a CommonMark code block. It is
**not** exempt: the checker scans indented lines as prose, so this file must produce violations and
the report must name the remedy — fence the quotation instead of indenting it.

    // Panel: 360x360 round AMOLED, 16.7 million colors, no backlight
    static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {

The same two lines inside a fenced block are exempt; `code_context_exempt.md` proves that. This
file exists so the difference is discoverable from the output rather than only from the checker's
docstring.
