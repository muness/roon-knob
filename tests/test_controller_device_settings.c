// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "controller_device_settings.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "controller_config.h"
#include "platform/platform_task.h"

static uint32_t endpoint = 1;
static bool defer_ui, reject_ui;
static platform_task_fn_t queued;
static void *queued_arg;
bool controller_config_capture_endpoint_token(controller_config_endpoint_token_t *out) {
    memset(out, 0, sizeof(*out)); out->generation = endpoint; return true;
}
bool platform_task_post_to_ui(platform_task_fn_t fn, void *arg) {
    if (reject_ui) return false;
    if (defer_ui) { assert(!queued); queued=fn; queued_arg=arg; } else fn(arg);
    return true;
}
static void drain(void) {
    if (queued) { platform_task_fn_t fn=queued; queued=NULL; fn(queued_arg); }
}


/* Fake platform hook: records every call so the tests can prove what reaches a target. */
static int hook_calls;
static controller_device_settings_t hook_last;
static bool hook_result = true;
static void (*during_apply)(void);
bool platform_device_settings_apply(const controller_device_settings_t *settings) {
    hook_calls++;
    hook_last = *settings;
    if (during_apply) { void (*hook)(void)=during_apply; during_apply=NULL; hook(); }
    return hook_result;
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

/* A target that could not store or queue a value must not lose it: the bridge's config hash was
 * already accepted and will not be served again, so the device keeps the change itself and retries
 * a bounded number of times. */
static void test_failed_apply_is_retried_then_delivered(void) {
    bool any;
    hook_calls = 0; hook_result = false;
    controller_device_settings_t s = parse("{\"brightness\":40}", &any);
    controller_device_settings_submit(&s, endpoint);
    assert(hook_calls == 1);
    controller_device_settings_retry_pending();
    controller_device_settings_retry_pending();
    assert(hook_calls == 3 && hook_last.brightness == 40);
    hook_result = true;
    controller_device_settings_retry_pending();
    assert(hook_calls == 4);
    controller_device_settings_retry_pending(); /* delivered: nothing left to retry */
    assert(hook_calls == 4);
}

static void test_retry_is_bounded(void) {
    bool any;
    hook_calls = 0; hook_result = false;
    controller_device_settings_t s = parse("{\"show_ip\":true}", &any);
    controller_device_settings_submit(&s, endpoint);
    for (int i = 0; i < 50; i++) controller_device_settings_retry_pending();
    int attempts = hook_calls;
    assert(attempts > 1 && attempts <= 1 + CONTROLLER_DEVICE_SETTINGS_MAX_RETRIES);
    controller_device_settings_retry_pending(); /* gave up: no unbounded replay */
    assert(hook_calls == attempts);
}

static void test_newer_submission_replaces_the_pending_one(void) {
    bool any;
    hook_calls = 0; hook_result = false;
    controller_device_settings_t old = parse("{\"brightness\":10}", &any);
    controller_device_settings_submit(&old, endpoint);
    controller_device_settings_t fresh = parse("{\"brightness\":80}", &any);
    hook_result = true;
    controller_device_settings_submit(&fresh, endpoint);
    assert(hook_last.brightness == 80);
    int calls = hook_calls;
    controller_device_settings_retry_pending(); /* the stale value is never replayed over it */
    assert(hook_calls == calls);
}

static void test_nothing_present_is_not_pending(void) {
    bool any;
    hook_calls = 0; hook_result = false;
    controller_device_settings_t s = parse("{\"zone\":1}", &any);
    controller_device_settings_submit(&s, endpoint);
    controller_device_settings_retry_pending();
    assert(hook_calls == 0);
    hook_result = true;
}

static void replace_during_apply(void) {
    controller_device_settings_t fresh={.present=CDS_BRIGHTNESS,.brightness=90};
    controller_device_settings_submit(&fresh,endpoint);
}
static void test_old_completion_cannot_complete_new_desire(void) {
    controller_device_settings_t old={.present=CDS_BRIGHTNESS,.brightness=10};
    defer_ui=true; hook_result=true; hook_calls=0;
    during_apply=replace_during_apply;
    controller_device_settings_submit(&old,endpoint); drain();
    assert(hook_calls==1 && hook_last.brightness==10);
    assert(controller_device_settings_status()==CDS_STATUS_PENDING);
    drain(); assert(hook_calls==2 && hook_last.brightness==90);
    assert(controller_device_settings_status()==CDS_STATUS_APPLIED);
    defer_ui=false;
}

static void test_actor_authority(void) {
    controller_device_settings_t old = {.present=CDS_BRIGHTNESS, .brightness=10};
    controller_device_settings_t fresh = {.present=CDS_BRIGHTNESS, .brightness=80};
    controller_device_settings_t empty = {0};
    defer_ui=true; hook_result=true; hook_calls=0;
    controller_device_settings_submit(&old, endpoint);
    assert(hook_calls == 0 && controller_device_settings_status() == CDS_STATUS_PENDING);
    endpoint += 2; drain(); /* ABA invalidates the original request */
    assert(hook_calls == 0 && controller_device_settings_status() == CDS_STATUS_CANCELLED);
    controller_device_settings_submit(&old, endpoint);
    controller_device_settings_submit(&empty, endpoint); drain();
    assert(hook_calls == 0);
    controller_device_settings_submit(&old, endpoint);
    controller_device_settings_local_edit(CDS_BRIGHTNESS); drain();
    controller_device_settings_retry_pending(); drain();
    assert(hook_calls == 0); /* same-SHA retry cannot resurrect it */
    controller_device_settings_submit(&fresh, endpoint); drain();
    assert(hook_calls == 1 && hook_last.brightness == 80);
    controller_device_settings_submit(&old, endpoint);
    assert(controller_device_settings_submit_local(&fresh)); drain();
    assert(hook_calls == 2 && hook_last.brightness == 80);
    reject_ui=true;
    controller_device_settings_submit(&old, endpoint);
    assert(hook_calls == 2 && controller_device_settings_status() == CDS_STATUS_PENDING);
    reject_ui=false; controller_device_settings_retry_pending(); drain();
    assert(hook_calls == 3 && hook_last.brightness == 10);
    defer_ui=false;
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
    test_failed_apply_is_retried_then_delivered();
    test_retry_is_bounded();
    test_newer_submission_replaces_the_pending_one();
    test_nothing_present_is_not_pending();
    test_actor_authority();
    test_old_completion_cannot_complete_new_desire();
    test_firmware_update();
    puts("controller device settings checks passed");
    return 0;
}
