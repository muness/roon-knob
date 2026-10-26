// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "controller_device_settings.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

/* Fake platform hook: records every call so the tests can prove what reaches a target. */
static int hook_calls;
static controller_device_settings_t hook_last;
void platform_device_settings_apply(const controller_device_settings_t *settings) {
    hook_calls++;
    hook_last = *settings;
}

static controller_device_settings_t parse(const char *json, bool *any) {
    cJSON *root = cJSON_Parse(json);
    assert(root);
    controller_device_settings_t s;
    bool got = controller_device_settings_parse(root, &s);
    assert(got == (s.present != 0));
    cJSON_Delete(root);
    if (any) *any = got;
    return s;
}

static bool firmware(const char *json, char *from, size_t cap) {
    cJSON *root = cJSON_Parse(json);
    assert(root);
    bool requested = controller_firmware_update_parse(root, from, cap);
    cJSON_Delete(root);
    return requested;
}

static void test_valid_values(void) {
    bool any;
    controller_device_settings_t s = parse(
        "{\"show_ip\":false,\"brightness\":5,\"body_enabled\":true,"
        "\"sound_enabled\":false,\"sound_volume\":2}", &any);
    assert(any);
    assert(s.present == (CDS_SHOW_IP | CDS_BRIGHTNESS | CDS_BODY_ENABLED |
                         CDS_SOUND_ENABLED | CDS_SOUND_VOLUME));
    assert(s.show_ip == false && s.brightness == 5 && s.body_enabled == true);
    assert(s.sound_enabled == false && s.sound_volume == 2);
    s = parse("{\"brightness\":100,\"sound_volume\":0}", &any);
    assert(s.present == (CDS_BRIGHTNESS | CDS_SOUND_VOLUME));
    assert(s.brightness == 100 && s.sound_volume == 0);
}

static void test_absent_and_unknown(void) {
    bool any;
    controller_device_settings_t s = parse("{}", &any);
    assert(!any && s.present == 0);
    s = parse("{\"zone\":\"x\",\"future_key\":[1,2],\"brightness\":50}", &any);
    assert(any && s.present == CDS_BRIGHTNESS && s.brightness == 50);
}

static void test_null_wrong_type_out_of_range(void) {
    bool any;
    parse("{\"show_ip\":null,\"brightness\":null,\"body_enabled\":null,"
          "\"sound_enabled\":null,\"sound_volume\":null}", &any);
    assert(!any);
    parse("{\"show_ip\":1,\"body_enabled\":\"true\",\"sound_enabled\":0,"
          "\"brightness\":\"50\",\"sound_volume\":true}", &any);
    assert(!any);
    parse("{\"brightness\":4}", &any); assert(!any);
    parse("{\"brightness\":101}", &any); assert(!any);
    parse("{\"brightness\":0}", &any); assert(!any);
    parse("{\"brightness\":-1}", &any); assert(!any);
    parse("{\"brightness\":50.5}", &any); assert(!any);
    parse("{\"brightness\":1e300}", &any); assert(!any);
    parse("{\"sound_volume\":3}", &any); assert(!any);
    parse("{\"sound_volume\":-1}", &any); assert(!any);
    parse("{\"sound_volume\":1.5}", &any); assert(!any);
    /* One bad key never poisons its valid neighbours. */
    controller_device_settings_t s = parse("{\"brightness\":500,\"show_ip\":true}", &any);
    assert(any && s.present == CDS_SHOW_IP && s.show_ip);
    /* Integral floats are accepted as the integer they are. */
    s = parse("{\"brightness\":50.0}", &any);
    assert(any && s.brightness == 50);
}

static void test_not_an_object(void) {
    controller_device_settings_t s;
    assert(!controller_device_settings_parse(NULL, &s) && s.present == 0);
    cJSON *array = cJSON_Parse("[1]");
    assert(!controller_device_settings_parse(array, &s) && s.present == 0);
    char from[32];
    assert(!controller_firmware_update_parse(array, from, sizeof(from)));
    assert(!controller_firmware_update_parse(NULL, from, sizeof(from)));
    cJSON_Delete(array);
}

static void test_apply_only_present_and_valid(void) {
    hook_calls = 0;
    bool any;
    controller_device_settings_t s = parse("{\"zone\":1}", &any);
    controller_device_settings_apply(&s);
    assert(hook_calls == 0);
    s = parse("{\"brightness\":900,\"sound_volume\":9}", &any);
    controller_device_settings_apply(&s);
    assert(hook_calls == 0);
    s = parse("{\"brightness\":40,\"sound_volume\":9}", &any);
    controller_device_settings_apply(&s);
    assert(hook_calls == 1 && hook_last.present == CDS_BRIGHTNESS && hook_last.brightness == 40);
    controller_device_settings_apply(NULL);
    assert(hook_calls == 1);
}

static void test_firmware_update(void) {
    char from[32] = "junk";
    assert(firmware("{\"firmware_update\":{\"action\":\"install\",\"from_version\":\"4.0.0-beta.9\"}}",
                    from, sizeof(from)));
    assert(!strcmp(from, "4.0.0-beta.9"));
    assert(firmware("{\"firmware_update\":{\"action\":\"install\",\"from_version\":\"\"}}", from, sizeof(from)));
    assert(from[0] == '\0');
    /* Anything else is no request, and the output is left empty. */
    static const char *bad[] = {
        "{}",
        "{\"firmware_update\":null}",
        "{\"firmware_update\":\"install\"}",
        "{\"firmware_update\":true}",
        "{\"firmware_update\":[]}",
        "{\"firmware_update\":{}}",
        "{\"firmware_update\":{\"action\":\"cancel\",\"from_version\":\"1.0.0\"}}",
        "{\"firmware_update\":{\"action\":\"INSTALL\",\"from_version\":\"1.0.0\"}}",
        "{\"firmware_update\":{\"action\":null,\"from_version\":\"1.0.0\"}}",
        "{\"firmware_update\":{\"action\":1,\"from_version\":\"1.0.0\"}}",
        "{\"firmware_update\":{\"from_version\":\"1.0.0\"}}",
        "{\"firmware_update\":{\"action\":\"install\"}}",
        "{\"firmware_update\":{\"action\":\"install\",\"from_version\":null}}",
        "{\"firmware_update\":{\"action\":\"install\",\"from_version\":7}}",
        "{\"firmware_update\":{\"action\":\"install\",\"from_version\":\"0123456789012345678901234567890123456789\"}}",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        strcpy(from, "junk");
        assert(!firmware(bad[i], from, sizeof(from)));
        assert(from[0] == '\0');
    }
    /* A version exactly filling the buffer is rejected, not truncated. */
    assert(!firmware("{\"firmware_update\":{\"action\":\"install\",\"from_version\":\"0123456789\"}}", from, 10));
    assert(!controller_firmware_update_parse(NULL, NULL, 0));
}

int main(void) {
    test_valid_values();
    test_absent_and_unknown();
    test_null_wrong_type_out_of_range();
    test_not_an_object();
    test_apply_only_present_and_valid();
    test_firmware_update();
    puts("controller device settings checks passed");
    return 0;
}
