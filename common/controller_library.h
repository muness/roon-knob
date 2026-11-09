#pragma once
#include <stdbool.h>
#include <stddef.h>
#define CONTROLLER_LIBRARY_PAGE_SIZE 50
/* Two server pages may be retained by the round touch browser. The wire
 * decoder still accepts at most PAGE_SIZE entries per response. */
#define CONTROLLER_LIBRARY_CAPACITY (2 * CONTROLLER_LIBRARY_PAGE_SIZE)
#define CONTROLLER_LIBRARY_HANDLE_SIZE 513
/* Provider handles are opaque, retained only for the current response. */
typedef struct {
  char title[301], subtitle[301];
  char location[CONTROLLER_LIBRARY_HANDLE_SIZE],
      ref[CONTROLLER_LIBRARY_HANDLE_SIZE];
  char image[600];
  bool requires_input;
} controller_library_item_t;
typedef struct {
  controller_library_item_t items[CONTROLLER_LIBRARY_CAPACITY];
  int count, next_offset;
  char notice[256];
  bool accepted;
} controller_library_page_t;
bool controller_library_parse(const char *json, const char *zone, bool mutation,
                              controller_library_page_t *out);
char *controller_library_body(const char *zone, const char *location,
                              int offset, const char *ref, const char *action);
bool controller_library_image_path(const char *path);
