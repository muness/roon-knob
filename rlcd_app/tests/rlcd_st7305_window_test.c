#include "rlcd_st7305_window.h"

#include <assert.h>
#include <stdio.h>

static void full_screen_maps_to_full_controller_memory(void) {
    rlcd_st7305_window_t window;
    assert(rlcd_st7305_window_from_dirty(0, 0, 399, 299, &window));
    assert(window.column_start == 0x12);
    assert(window.column_end == 0x2a);
    assert(window.page_start == 0);
    assert(window.page_end == 199);
    assert(window.framebuffer_byte_start == 0);
    assert(window.bytes_per_page == 75);
    assert(window.page_count == 200);
    assert(window.transfer_size == 15000);
}

static void seek_strip_maps_to_one_column_block(void) {
    rlcd_st7305_window_t window;
    assert(rlcd_st7305_window_from_dirty(14, 280, 385, 285, &window));
    assert(window.column_start == 0x29);
    assert(window.column_end == 0x29);
    assert(window.page_start == 7);
    assert(window.page_end == 192);
    assert(window.framebuffer_byte_start == 3);
    assert(window.bytes_per_page == 3);
    assert(window.transfer_size == 558);
}

static void opposite_corners_do_not_alias(void) {
    rlcd_st7305_window_t top_right;
    rlcd_st7305_window_t bottom_left;
    assert(rlcd_st7305_window_from_dirty(399, 0, 399, 0, &top_right));
    assert(top_right.column_start == 0x12);
    assert(top_right.page_start == 199);
    assert(top_right.transfer_size == 3);
    assert(rlcd_st7305_window_from_dirty(0, 299, 0, 299, &bottom_left));
    assert(bottom_left.column_start == 0x2a);
    assert(bottom_left.page_start == 0);
    assert(bottom_left.transfer_size == 3);
}

static void invalid_bounds_fail_closed(void) {
    rlcd_st7305_window_t window;
    assert(!rlcd_st7305_window_from_dirty(-1, 0, 1, 1, &window));
    assert(!rlcd_st7305_window_from_dirty(0, 0, 400, 1, &window));
    assert(!rlcd_st7305_window_from_dirty(4, 4, 3, 5, &window));
}

int main(void) {
    full_screen_maps_to_full_controller_memory();
    seek_strip_maps_to_one_column_block();
    opposite_corners_do_not_alias();
    invalid_bounds_fail_closed();
    puts("rlcd_st7305_window_test: ok");
    return 0;
}
