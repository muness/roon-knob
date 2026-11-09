#include "cJSON.h"
#include "controller_library.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
  controller_library_page_t *page = calloc(1, sizeof(*page));
  assert(page);
  const char *valid =
      "{\"scope\":{\"zone_id\":\"roon:z\"},\"outcome\":\"ok\",\"data\":{"
      "\"items\":[{\"title\":\"Album\",\"location\":\"opaque-location\","
      "\"ref\":\"opaque-ref\",\"image\":\"/api/collections/"
      "image?ref=img_123\"}],\"next_offset\":12}}";
  assert(controller_library_parse(valid, "roon:z", false, page));
  assert(page->count == 1 && page->next_offset == 12);
  assert(!strcmp(page->items[0].location, "opaque-location") &&
         !strcmp(page->items[0].ref, "opaque-ref"));
  assert(!controller_library_parse(valid, "roon:other", false, page) &&
         page->count == 0);
  assert(!controller_library_parse("not json", "roon:z", false, page));
  assert(!controller_library_parse(
      "{\"scope\":{\"zone_id\":\"roon:z\"},\"outcome\":\"invalid\",\"refusal\":"
      "{\"detail\":\"Expired item\"}}",
      "roon:z", true, page));
  assert(!strcmp(page->notice, "Expired item") && !page->accepted);
  assert(controller_library_parse(
             "{\"scope\":{\"zone_id\":\"roon:z\"},\"outcome\":\"accepted\"}",
             "roon:z", true, page) &&
         page->accepted);
  assert(
      !controller_library_parse("{\"scope\":{\"zone_id\":\"roon:z\"},"
                                "\"outcome\":\"ok\",\"data\":{\"items\":[{}]}}",
                                "roon:z", false, page));
  assert(controller_library_parse(
      "{\"scope\":{\"zone_id\":\"roon:z\"},\"outcome\":\"ok\",\"data\":{\"items\":["
      "{\"title\":\"Input\",\"requires_input\":true},{\"title\":\"Prompt\",\"input_prompt\":{}},"
      "{\"title\":\"Ordinary\"}]}}", "roon:z", false, page));
  assert(page->items[0].requires_input && page->items[1].requires_input && !page->items[2].requires_input);
  cJSON *long_root = cJSON_Parse(valid);
  cJSON *long_row = cJSON_GetArrayItem(
      cJSON_GetObjectItemCaseSensitive(
          cJSON_GetObjectItemCaseSensitive(long_root, "data"), "items"),
      0);
  char long_title[310];
  memset(long_title, 'a', 299);
  memcpy(long_title + 299, "\xf0\x9f\x8e\xb5", 5);
  cJSON_ReplaceItemInObjectCaseSensitive(long_row, "title",
                                         cJSON_CreateString(long_title));
  char *long_json = cJSON_PrintUnformatted(long_root);
  assert(controller_library_parse(long_json, "roon:z", false, page));
  assert(strlen(page->items[0].title) == 299);
  cJSON_free(long_json);
  cJSON_Delete(long_root);
  cJSON *r = cJSON_Parse(valid);
  cJSON *items = cJSON_GetObjectItemCaseSensitive(
      cJSON_GetObjectItemCaseSensitive(r, "data"), "items");
  for (int i = 0; i < CONTROLLER_LIBRARY_PAGE_SIZE; i++)
    cJSON_AddItemToArray(items,
                         cJSON_Duplicate(cJSON_GetArrayItem(items, 0), true));
  char *large = cJSON_PrintUnformatted(r);
  assert(!controller_library_parse(large, "roon:z", false, page) &&
         page->count == 0);
  cJSON_free(large);
  cJSON_Delete(r);
  assert(
      controller_library_image_path("/api/collections/image?ref=abc%2D_123"));
  assert(!controller_library_image_path("http://other/image"));
  assert(!controller_library_image_path(
      "/api/collections/image?ref=abc&host=other"));
  assert(!controller_library_image_path("/api/collections/image?ref="));
  char *body =
      controller_library_body("roon:z", "opaque\"location", 12, NULL, NULL);
  assert(body);
  r = cJSON_Parse(body);
  assert(r);
  assert(!strcmp(cJSON_GetObjectItemCaseSensitive(r, "location")->valuestring,
                 "opaque\"location"));
  assert(cJSON_GetObjectItemCaseSensitive(r, "limit")->valueint == CONTROLLER_LIBRARY_PAGE_SIZE);
  cJSON_Delete(r);
  cJSON_free(body);
  assert(!controller_library_body("roon:z", "", 0, "ref", "delete"));
  char huge[514];
  memset(huge, 'a', sizeof(huge) - 1);
  huge[513] = 0;
  assert(!controller_library_body("roon:z", huge, 0, NULL, NULL));
  body = controller_library_body("roon:z", "", 0, "opaque-ref", "queue");
  assert(body);
  r = cJSON_Parse(body);
  assert(!strcmp(cJSON_GetObjectItemCaseSensitive(r, "action")->valuestring,
                 "queue"));
  assert(!cJSON_HasObjectItem(r, "location"));
  cJSON_Delete(r);
  cJSON_free(body);
  free(page);
  puts("controller library contracts passed");
  return 0;
}
