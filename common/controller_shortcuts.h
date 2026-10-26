// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define CONTROLLER_SHORTCUTS_PAGE_SIZE 12
/* Stable bridge-owned identities. No provider browse handles or HID mappings. */
typedef struct {
    char id[64], name[161], provider[33];
} controller_shortcuts_item_t;
typedef struct {
    controller_shortcuts_item_t items[CONTROLLER_SHORTCUTS_PAGE_SIZE];
    int count, next_offset;
    char notice[256];
    bool accepted;
} controller_shortcuts_page_t;
