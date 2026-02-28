#include "manifest_parse.h"

#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform_log.h"

#define TAG "manifest_parse"

// ── Helpers ─────────────────────────────────────────────────────────────────

static void safe_strcpy(char *dst, const char *src, size_t dst_size) {
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dst_size - 1);
  dst[dst_size - 1] = '\0';
}

static text_style_t parse_text_style(const char *style) {
  if (!style)
    return TEXT_STYLE_DETAIL;
  if (strcmp(style, "title") == 0)
    return TEXT_STYLE_TITLE;
  if (strcmp(style, "subtitle") == 0)
    return TEXT_STYLE_SUBTITLE;
  return TEXT_STYLE_DETAIL;
}

static screen_type_t parse_screen_type(const char *type) {
  if (!type)
    return SCREEN_TYPE_UNKNOWN;
  if (strcmp(type, "media") == 0)
    return SCREEN_TYPE_MEDIA;
  if (strcmp(type, "list") == 0)
    return SCREEN_TYPE_LIST;
  if (strcmp(type, "card") == 0)
    return SCREEN_TYPE_CARD;
  if (strcmp(type, "progress") == 0)
    return SCREEN_TYPE_PROGRESS;
  if (strcmp(type, "status") == 0)
    return SCREEN_TYPE_STATUS;
  return SCREEN_TYPE_UNKNOWN;
}

// ── Fast state parsing ──────────────────────────────────────────────────────

static bool parse_transport(cJSON *obj, manifest_transport_t *out) {
  if (!obj)
    return false;
  out->play = cJSON_IsTrue(cJSON_GetObjectItem(obj, "play"));
  out->pause = cJSON_IsTrue(cJSON_GetObjectItem(obj, "pause"));
  out->next = cJSON_IsTrue(cJSON_GetObjectItem(obj, "next"));
  out->prev = cJSON_IsTrue(cJSON_GetObjectItem(obj, "prev"));
  return true;
}

static bool parse_fast_from_obj(cJSON *fast_obj, manifest_fast_t *out) {
  if (!fast_obj)
    return false;

  memset(out, 0, sizeof(*out));

  cJSON *item;

  item = cJSON_GetObjectItem(fast_obj, "zone_id");
  if (cJSON_IsString(item))
    safe_strcpy(out->zone_id, item->valuestring, sizeof(out->zone_id));

  out->is_playing = cJSON_IsTrue(cJSON_GetObjectItem(fast_obj, "is_playing"));

  item = cJSON_GetObjectItem(fast_obj, "volume");
  if (cJSON_IsNumber(item))
    out->volume = (float)item->valuedouble;

  item = cJSON_GetObjectItem(fast_obj, "volume_min");
  if (cJSON_IsNumber(item))
    out->volume_min = (float)item->valuedouble;

  item = cJSON_GetObjectItem(fast_obj, "volume_max");
  if (cJSON_IsNumber(item))
    out->volume_max = (float)item->valuedouble;

  item = cJSON_GetObjectItem(fast_obj, "volume_step");
  if (cJSON_IsNumber(item))
    out->volume_step = (float)item->valuedouble;
  else
    out->volume_step = 1.0f;

  item = cJSON_GetObjectItem(fast_obj, "volume_type");
  if (cJSON_IsString(item))
    safe_strcpy(out->volume_type, item->valuestring, sizeof(out->volume_type));

  item = cJSON_GetObjectItem(fast_obj, "seek_position");
  if (cJSON_IsNumber(item))
    out->seek_position = item->valueint;

  item = cJSON_GetObjectItem(fast_obj, "length");
  if (cJSON_IsNumber(item))
    out->length = item->valueint;

  parse_transport(cJSON_GetObjectItem(fast_obj, "transport"), &out->transport);

  return true;
}

// ── Text lines parsing ──────────────────────────────────────────────────────

static int parse_text_lines(cJSON *lines_arr, manifest_text_line_t *out,
                            int max_lines) {
  if (!cJSON_IsArray(lines_arr))
    return 0;

  int count = 0;
  cJSON *line_obj;
  cJSON_ArrayForEach(line_obj, lines_arr) {
    if (count >= max_lines)
      break;

    cJSON *text = cJSON_GetObjectItem(line_obj, "text");
    cJSON *style = cJSON_GetObjectItem(line_obj, "style");

    if (cJSON_IsString(text)) {
      safe_strcpy(out[count].text, text->valuestring, sizeof(out[count].text));
    }
    out[count].style =
        parse_text_style(cJSON_IsString(style) ? style->valuestring : NULL);
    count++;
  }
  return count;
}

// ── Screen parsing ──────────────────────────────────────────────────────────

static bool parse_media_screen(cJSON *obj, manifest_media_t *out) {
  memset(out, 0, sizeof(*out));

  cJSON *item;

  item = cJSON_GetObjectItem(obj, "image_url");
  if (cJSON_IsString(item))
    safe_strcpy(out->image_url, item->valuestring, sizeof(out->image_url));

  item = cJSON_GetObjectItem(obj, "image_key");
  if (cJSON_IsString(item))
    safe_strcpy(out->image_key, item->valuestring, sizeof(out->image_key));

  item = cJSON_GetObjectItem(obj, "background_color");
  if (cJSON_IsString(item))
    safe_strcpy(out->bg_color, item->valuestring, sizeof(out->bg_color));

  out->line_count = parse_text_lines(cJSON_GetObjectItem(obj, "lines"),
                                     out->lines, MANIFEST_MAX_LINES);

  return true;
}

static bool parse_list_screen(cJSON *obj, manifest_list_t *out) {
  memset(out, 0, sizeof(*out));

  cJSON *item = cJSON_GetObjectItem(obj, "title");
  if (cJSON_IsString(item))
    safe_strcpy(out->title, item->valuestring, sizeof(out->title));

  cJSON *items_arr = cJSON_GetObjectItem(obj, "items");
  if (!cJSON_IsArray(items_arr))
    return true; // Empty list is valid

  cJSON *list_item;
  cJSON_ArrayForEach(list_item, items_arr) {
    if (out->item_count >= MANIFEST_MAX_LIST_ITEMS)
      break;

    manifest_list_item_t *li = &out->items[out->item_count];
    memset(li, 0, sizeof(*li));

    item = cJSON_GetObjectItem(list_item, "id");
    if (cJSON_IsString(item))
      safe_strcpy(li->id, item->valuestring, sizeof(li->id));

    item = cJSON_GetObjectItem(list_item, "label");
    if (cJSON_IsString(item))
      safe_strcpy(li->label, item->valuestring, sizeof(li->label));

    item = cJSON_GetObjectItem(list_item, "sublabel");
    if (cJSON_IsString(item))
      safe_strcpy(li->sublabel, item->valuestring, sizeof(li->sublabel));

    li->selected = cJSON_IsTrue(cJSON_GetObjectItem(list_item, "selected"));

    out->item_count++;
  }
  return true;
}

static bool parse_card_screen(cJSON *obj, manifest_card_t *out) {
  memset(out, 0, sizeof(*out));
  out->line_count = parse_text_lines(cJSON_GetObjectItem(obj, "lines"),
                                     out->lines, MANIFEST_MAX_LINES);
  return true;
}

static bool parse_progress_screen(cJSON *obj, manifest_progress_t *out) {
  memset(out, 0, sizeof(*out));

  cJSON *item = cJSON_GetObjectItem(obj, "label");
  if (cJSON_IsString(item))
    safe_strcpy(out->label, item->valuestring, sizeof(out->label));

  item = cJSON_GetObjectItem(obj, "progress");
  if (cJSON_IsNumber(item))
    out->progress = (float)item->valuedouble;

  return true;
}

static bool parse_status_screen(cJSON *obj, manifest_status_t *out) {
  memset(out, 0, sizeof(*out));

  cJSON *item = cJSON_GetObjectItem(obj, "message");
  if (cJSON_IsString(item))
    safe_strcpy(out->message, item->valuestring, sizeof(out->message));

  item = cJSON_GetObjectItem(obj, "icon");
  if (cJSON_IsString(item))
    safe_strcpy(out->icon, item->valuestring, sizeof(out->icon));

  return true;
}

/// Parse "controls" array on a screen (e.g. ["prev", "play", "next", "mute"]).
/// Optional — if absent, controls_count stays 0 (show all defaults).
static void parse_screen_controls(cJSON *screen_obj, manifest_screen_t *out) {
  cJSON *controls_arr = cJSON_GetObjectItem(screen_obj, "controls");
  if (!cJSON_IsArray(controls_arr))
    return;

  cJSON *item;
  cJSON_ArrayForEach(item, controls_arr) {
    if (out->controls_count >= MAX_CONTROLS)
      break;
    if (cJSON_IsString(item)) {
      safe_strcpy(out->controls[out->controls_count], item->valuestring,
                  MAX_ACTION_LEN);
      out->controls_count++;
    }
  }
}

/// Parse a single action object: {"action": "...", "params": {...}}
static void parse_action(const cJSON *obj, manifest_action_t *out) {
    memset(out, 0, sizeof(*out));
    if (!obj || !cJSON_IsObject(obj)) return;
    const cJSON *action = cJSON_GetObjectItem(obj, "action");
    if (action && cJSON_IsString(action)) {
        safe_strcpy(out->action, action->valuestring, MAX_ACTION_LEN);
    }
    const cJSON *params = cJSON_GetObjectItem(obj, "params");
    if (params && !cJSON_IsNull(params)) {
        out->has_params = true;
        char *json_str = cJSON_PrintUnformatted(params);
        if (json_str) {
            safe_strcpy(out->params_json, json_str, MAX_PARAMS_JSON);
            cJSON_free(json_str);
        }
    }
}

/// Parse display object: {"icon": "...", "label": "...", "active": true}
static void parse_display(const cJSON *obj, manifest_display_t *out) {
    memset(out, 0, sizeof(*out));
    if (!obj || !cJSON_IsObject(obj)) return;
    const cJSON *icon = cJSON_GetObjectItem(obj, "icon");
    if (icon && cJSON_IsString(icon)) {
        safe_strcpy(out->icon, icon->valuestring, MAX_ICON_LEN);
    }
    const cJSON *label = cJSON_GetObjectItem(obj, "label");
    if (label && cJSON_IsString(label)) {
        safe_strcpy(out->label, label->valuestring, MAX_LABEL_LEN);
    }
    const cJSON *active = cJSON_GetObjectItem(obj, "active");
    if (active && cJSON_IsBool(active)) {
        out->active = cJSON_IsTrue(active);
    }
}

/// Parse elements array from a screen object.
static void parse_screen_elements(const cJSON *screen_obj, manifest_screen_t *scr) {
    scr->element_count = 0;
    const cJSON *elements = cJSON_GetObjectItem(screen_obj, "elements");
    if (!elements || !cJSON_IsArray(elements)) return;

    int count = cJSON_GetArraySize(elements);
    if (count > MAX_ELEMENTS) count = MAX_ELEMENTS;

    for (int i = 0; i < count; i++) {
        const cJSON *elem = cJSON_GetArrayItem(elements, i);
        if (!elem || !cJSON_IsObject(elem)) continue;

        manifest_element_t *el = &scr->elements[scr->element_count];
        memset(el, 0, sizeof(*el));

        parse_display(cJSON_GetObjectItem(elem, "display"), &el->display);

        const cJSON *on_tap = cJSON_GetObjectItem(elem, "on_tap");
        if (on_tap && cJSON_IsObject(on_tap)) {
            el->has_on_tap = true;
            parse_action(on_tap, &el->on_tap);
        }

        const cJSON *on_long_press = cJSON_GetObjectItem(elem, "on_long_press");
        if (on_long_press && cJSON_IsObject(on_long_press)) {
            el->has_on_long_press = true;
            parse_action(on_long_press, &el->on_long_press);
        }

        scr->element_count++;
    }
}

/// Parse per-screen encoder config.
static void parse_screen_encoder(const cJSON *screen_obj, manifest_screen_t *scr) {
    scr->has_encoder = false;
    const cJSON *enc = cJSON_GetObjectItem(screen_obj, "encoder");
    if (!enc || !cJSON_IsObject(enc)) return;

    scr->has_encoder = true;
    memset(&scr->encoder, 0, sizeof(scr->encoder));

    parse_action(cJSON_GetObjectItem(enc, "cw"), &scr->encoder.cw);
    parse_action(cJSON_GetObjectItem(enc, "ccw"), &scr->encoder.ccw);

    const cJSON *press = cJSON_GetObjectItem(enc, "press");
    if (press && cJSON_IsObject(press)) {
        scr->encoder.has_press = true;
        parse_action(press, &scr->encoder.press);
    }

    const cJSON *long_press = cJSON_GetObjectItem(enc, "long_press");
    if (long_press && cJSON_IsObject(long_press)) {
        scr->encoder.has_long_press = true;
        parse_action(long_press, &scr->encoder.long_press);
    }
}

static bool parse_screen(cJSON *screen_obj, manifest_screen_t *out) {
  memset(out, 0, sizeof(*out));

  cJSON *type_item = cJSON_GetObjectItem(screen_obj, "type");
  cJSON *id_item = cJSON_GetObjectItem(screen_obj, "id");

  if (!cJSON_IsString(type_item))
    return false;

  out->type = parse_screen_type(type_item->valuestring);
  if (cJSON_IsString(id_item))
    safe_strcpy(out->id, id_item->valuestring, sizeof(out->id));

  // Parse optional controls array (config-driven button visibility)
  parse_screen_controls(screen_obj, out);

  // Parse v2 command-pattern elements and encoder (optional)
  parse_screen_elements(screen_obj, out);
  parse_screen_encoder(screen_obj, out);

  switch (out->type) {
  case SCREEN_TYPE_MEDIA:
    return parse_media_screen(screen_obj, &out->data.media);
  case SCREEN_TYPE_LIST:
    return parse_list_screen(screen_obj, &out->data.list);
  case SCREEN_TYPE_CARD:
    return parse_card_screen(screen_obj, &out->data.card);
  case SCREEN_TYPE_PROGRESS:
    return parse_progress_screen(screen_obj, &out->data.progress);
  case SCREEN_TYPE_STATUS:
    return parse_status_screen(screen_obj, &out->data.status);
  case SCREEN_TYPE_UNKNOWN:
    LOGI("Unknown screen type: %s", type_item->valuestring);
    return false;
  }
  return false;
}

// ── Nav parsing ─────────────────────────────────────────────────────────────

static void parse_nav(cJSON *nav_obj, manifest_nav_t *out) {
  memset(out, 0, sizeof(*out));

  if (!nav_obj)
    return;

  cJSON *order = cJSON_GetObjectItem(nav_obj, "order");
  if (cJSON_IsArray(order)) {
    cJSON *item;
    cJSON_ArrayForEach(item, order) {
      if (out->count >= MANIFEST_MAX_SCREENS)
        break;
      if (cJSON_IsString(item)) {
        safe_strcpy(out->order[out->count], item->valuestring, MANIFEST_MAX_ID);
        out->count++;
      }
    }
  }

  cJSON *def = cJSON_GetObjectItem(nav_obj, "default");
  if (cJSON_IsString(def)) {
    safe_strcpy(out->default_screen, def->valuestring,
                sizeof(out->default_screen));
  } else if (out->count > 0) {
    safe_strcpy(out->default_screen, out->order[0],
                sizeof(out->default_screen));
  }
}

// ── Public API ──────────────────────────────────────────────────────────────

bool manifest_parse(const char *json, size_t json_len, manifest_t *out) {
  if (!json || !out || json_len == 0)
    return false;
  memset(out, 0, sizeof(*out));

  cJSON *root = cJSON_ParseWithLength(json, json_len);
  if (!root) {
    LOGI("manifest_parse: JSON parse error");
    return false;
  }

  // Version
  cJSON *ver = cJSON_GetObjectItem(root, "version");
  out->version = cJSON_IsNumber(ver) ? (uint32_t)ver->valueint : 0;

  // SHA
  cJSON *sha = cJSON_GetObjectItem(root, "sha");
  if (cJSON_IsString(sha))
    safe_strcpy(out->sha, sha->valuestring, sizeof(out->sha));

  // Fast state
  if (!parse_fast_from_obj(cJSON_GetObjectItem(root, "fast"), &out->fast)) {
    LOGI("manifest_parse: failed to parse fast state");
    cJSON_Delete(root);
    return false;
  }

  // Screens
  cJSON *screens = cJSON_GetObjectItem(root, "screens");
  if (cJSON_IsArray(screens)) {
    cJSON *screen_obj;
    cJSON_ArrayForEach(screen_obj, screens) {
      if (out->screen_count >= MANIFEST_MAX_SCREENS)
        break;
      if (parse_screen(screen_obj, &out->screens[out->screen_count])) {
        out->screen_count++;
      }
    }
  }

  // Nav
  parse_nav(cJSON_GetObjectItem(root, "nav"), &out->nav);

  // Interactions (optional — config-driven input mapping)
  cJSON *interactions = cJSON_GetObjectItem(root, "interactions");
  if (cJSON_IsObject(interactions)) {
    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, interactions) {
      if (out->interactions.count >= MAX_INTERACTIONS)
        break;
      if (entry->string && cJSON_IsString(entry)) {
        interaction_mapping_t *m =
            &out->interactions.mappings[out->interactions.count];
        safe_strcpy(m->input, entry->string, sizeof(m->input));
        safe_strcpy(m->action, entry->valuestring, sizeof(m->action));
        out->interactions.count++;
      }
    }
    out->has_interactions = (out->interactions.count > 0);
    LOGI("Parsed %d interaction mappings", out->interactions.count);
  } else {
    out->has_interactions = false;
  }

  cJSON_Delete(root);
  return true;
}

bool manifest_parse_fast(const char *json, size_t json_len,
                         manifest_fast_t *out) {
  if (!json || !out || json_len == 0)
    return false;
  memset(out, 0, sizeof(*out));

  cJSON *root = cJSON_ParseWithLength(json, json_len);
  if (!root)
    return false;

  bool ok = parse_fast_from_obj(cJSON_GetObjectItem(root, "fast"), out);
  cJSON_Delete(root);
  return ok;
}

bool manifest_parse_sha(const char *json, size_t json_len, char *sha_out,
                        size_t sha_len) {
  if (!json || !sha_out || json_len == 0 || sha_len == 0)
    return false;
  sha_out[0] = '\0';

  cJSON *root = cJSON_ParseWithLength(json, json_len);
  if (!root)
    return false;

  cJSON *sha = cJSON_GetObjectItem(root, "sha");
  if (cJSON_IsString(sha)) {
    safe_strcpy(sha_out, sha->valuestring, sha_len);
    cJSON_Delete(root);
    return true;
  }

  cJSON_Delete(root);
  return false;
}

const char *manifest_lookup_interaction(const interactions_t *interactions,
                                         const char *input_name) {
  if (!interactions || !input_name)
    return NULL;
  for (int i = 0; i < interactions->count; i++) {
    if (strcmp(interactions->mappings[i].input, input_name) == 0) {
      return interactions->mappings[i].action;
    }
  }
  return NULL;
}
