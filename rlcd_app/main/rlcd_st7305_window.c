#include "rlcd_st7305_window.h"

#define RLCD_WINDOW_WIDTH 400
#define RLCD_WINDOW_HEIGHT 300
#define RLCD_COLUMN_BASE 0x12
#define RLCD_COLUMN_MIRROR 0x3c
#define RLCD_BYTES_PER_COLUMN 3

bool rlcd_st7305_window_from_dirty(int x1, int y1, int x2, int y2,
                                   rlcd_st7305_window_t *window) {
    if (!window || x1 < 0 || y1 < 0 || x2 < x1 || y2 < y1 ||
        x2 >= RLCD_WINDOW_WIDTH || y2 >= RLCD_WINDOW_HEIGHT) {
        return false;
    }

    int page_start = x1 >> 1;
    int page_end = x2 >> 1;
    int packed_byte_start = (RLCD_WINDOW_HEIGHT - 1 - y2) >> 2;
    int packed_byte_end = (RLCD_WINDOW_HEIGHT - 1 - y1) >> 2;
    int column_index_start = packed_byte_start / RLCD_BYTES_PER_COLUMN;
    int column_index_end = packed_byte_end / RLCD_BYTES_PER_COLUMN;

    /* MADCTL 0x48 mirrors the controller's column addresses. The full
     * 0x12..0x2a range looks identical either way, so this only becomes
     * observable on partial writes. */
    int nominal_column_start = RLCD_COLUMN_BASE + column_index_start;
    int nominal_column_end = RLCD_COLUMN_BASE + column_index_end;
    window->column_start = (uint8_t)(RLCD_COLUMN_MIRROR - nominal_column_end);
    window->column_end = (uint8_t)(RLCD_COLUMN_MIRROR - nominal_column_start);
    window->page_start = (uint8_t)page_start;
    window->page_end = (uint8_t)page_end;
    window->framebuffer_byte_start =
        (uint16_t)(column_index_start * RLCD_BYTES_PER_COLUMN);
    window->bytes_per_page = (uint16_t)(
        (column_index_end - column_index_start + 1) * RLCD_BYTES_PER_COLUMN);
    window->page_count = (uint16_t)(page_end - page_start + 1);
    window->transfer_size =
        (size_t)window->bytes_per_page * window->page_count;
    return true;
}
