#include "platform/platform_storage.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STORAGE_FILE "./rk_pc_store.json"

static const char *pc_default_bridge(void) {
    const char *env = getenv("ROON_BRIDGE_BASE");
    return (env && env[0]) ? env : "http://127.0.0.1:8088";
}

static void parse_field(const char *data, const char *key, char *out, size_t len) {
    if (!data || !key || !out) {
        return;
    }
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *found = strstr(data, pattern);
    if (!found) {
        return;
    }
    const char *colon = strchr(found, ':');
    if (!colon) {
        return;
    }
    const char *quote_start = strchr(colon, '\"');
    if (!quote_start) {
        return;
    }
    quote_start++;
    const char *end = strchr(quote_start, '\"');
    if (!end) {
        return;
    }
    size_t copy_len = (size_t)(end - quote_start);
    if (copy_len >= len) {
        copy_len = len - 1;
    }
    memcpy(out, quote_start, copy_len);
    out[copy_len] = '\0';
}

static bool parse_cfg_ver(const char *data, uint8_t *out) {
    if (!data || !out) {
        return false;
    }
    const char *found = strstr(data, "\"cfg_ver\"");
    if (!found) {
        return false;
    }
    const char *colon = strchr(found, ':');
    if (!colon) {
        return false;
    }
    int value = atoi(colon + 1);
    if (value <= 0) {
        return false;
    }
    *out = (uint8_t)value;
    return true;
}

bool platform_storage_load(rk_cfg_t *out) {
    if (!out) {
        return false;
    }
    FILE *f = fopen(STORAGE_FILE, "r");
    if (!f) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size <= 0) {
        fclose(f);
        return false;
    }
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return false;
    }
    fread(buf, 1, (size_t)size, f);
    buf[size] = '\0';
    fclose(f);

    memset(out, 0, sizeof(*out));
    parse_field(buf, "ssid", out->ssid, sizeof(out->ssid));
    parse_field(buf, "pass", out->pass, sizeof(out->pass));
    parse_field(buf, "bridge_base", out->bridge_base, sizeof(out->bridge_base));
    parse_field(buf, "zone_id", out->zone_id, sizeof(out->zone_id));
    uint8_t ver = 0;
    if (parse_cfg_ver(buf, &ver)) {
        out->cfg_ver = ver;
    }
    bool has_data = out->cfg_ver != 0 || out->bridge_base[0] || out->zone_id[0] || out->ssid[0];
    free(buf);
    return has_data;
}

bool platform_storage_save(const rk_cfg_t *in) {
    if (!in) {
        return false;
    }
    FILE *f = fopen(STORAGE_FILE, "w");
    if (!f) {
        return false;
    }
    fprintf(f, "{\n");
    fprintf(f, "  \"ssid\": \"%s\",\n", in->ssid);
    fprintf(f, "  \"pass\": \"%s\",\n", in->pass);
    fprintf(f, "  \"bridge_base\": \"%s\",\n", in->bridge_base);
    fprintf(f, "  \"zone_id\": \"%s\",\n", in->zone_id);
    fprintf(f, "  \"cfg_ver\": %u\n", in->cfg_ver);
    fprintf(f, "}\n");
    fflush(f);
    fclose(f);
    return true;
}

void platform_storage_defaults(rk_cfg_t *out) {
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    const char *env_ssid = getenv("ROON_KNOB_SSID");
    const char *env_pass = getenv("ROON_KNOB_PASS");
    const char *env_zone = getenv("ZONE_ID");
    if (env_ssid) {
        strncpy(out->ssid, env_ssid, sizeof(out->ssid) - 1);
    }
    if (env_pass) {
        strncpy(out->pass, env_pass, sizeof(out->pass) - 1);
    }
    strncpy(out->bridge_base, pc_default_bridge(), sizeof(out->bridge_base) - 1);
    if (env_zone) {
        strncpy(out->zone_id, env_zone, sizeof(out->zone_id) - 1);
    }
    out->cfg_ver = RK_CFG_CURRENT_VER;
}

void platform_storage_reset_wifi_only(rk_cfg_t *cfg) {
    if (!cfg) {
        return;
    }
    cfg->ssid[0] = '\0';
    cfg->pass[0] = '\0';
    platform_storage_save(cfg);
}
