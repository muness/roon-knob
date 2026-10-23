// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_stub.h"
#include "../common/platform/platform_http_esp.c"

static struct {
    const char *data, *encoding;
    esp_http_client_config_t config;
    bool duplicate_encoding;
    size_t size, offset;
    int64_t declared;
    int status, read_error, write_limit, writes, closed, cleaned, callback_count;
    bool complete, no_psram, no_memory, fail_grow, init_failure, open_failure;
    int internal_allocations, psram_allocations, method;
    char id[32], version[32], device[32], connection[32], accept_encoding[32], sent[128];
    size_t sent_len;
} fixture;

static void reset(const void *data, size_t size, int64_t declared) {
    memset(&fixture, 0, sizeof(fixture));
    fixture.data = data;
    fixture.size = size;
    fixture.declared = declared;
    fixture.status = 200;
    fixture.complete = true;
    fixture.write_limit = 128;
}
void *esp_http_client_init(const esp_http_client_config_t *config) {
    fixture.method = config->method;
    fixture.config = *config;
    return fixture.init_failure ? NULL : &fixture;
}
int esp_http_client_set_header(void *client, const char *name, const char *value) {
    char *dest = NULL;
    if (!strcmp(name, "X-Knob-Id")) dest = fixture.id;
    if (!strcmp(name, "X-Knob-Version")) dest = fixture.version;
    if (!strcmp(name, "X-Device-Type")) dest = fixture.device;
    if (!strcmp(name, "Connection")) dest = fixture.connection;
    if (!strcmp(name, "Accept-Encoding")) dest = fixture.accept_encoding;
    if (dest) snprintf(dest, 32, "%s", value);
    return 0;
}
int esp_http_client_open(void *client, int length) { return fixture.open_failure ? -1 : 0; }
int esp_http_client_write(void *client, const char *data, int length) {
    fixture.writes++;
    int n = length < fixture.write_limit ? length : fixture.write_limit;
    if (n > 0) {
        assert(fixture.sent_len + (size_t)n < sizeof(fixture.sent));
        memcpy(fixture.sent + fixture.sent_len, data, n);
        fixture.sent_len += n;
    }
    return n;
}
int64_t esp_http_client_fetch_headers(void *client) {
    if (fixture.encoding) {
        esp_http_client_event_t event = { .event_id=HTTP_EVENT_ON_HEADER,
            .header_key="cOnTent-eNcoding", .header_value=fixture.encoding,
            .user_data=fixture.config.user_data };
        assert(fixture.config.event_handler);
        fixture.config.event_handler(&event);
        if (fixture.duplicate_encoding) fixture.config.event_handler(&event);
    }
    return fixture.declared;
}
int esp_http_client_get_status_code(void *client) { return fixture.status; }
int esp_http_client_read(void *client, char *data, int length) {
    assert(length > 0);
    if (fixture.read_error) return -1;
    size_t n = fixture.size - fixture.offset;
    if (n > (size_t)length) n = length;
    if (n) memcpy(data, fixture.data + fixture.offset, n);
    fixture.offset += n;
    return (int)n;
}
bool esp_http_client_is_complete_data_received(void *client) {
    return fixture.complete && fixture.offset == fixture.size &&
        (fixture.declared <= 0 || fixture.offset == (uint64_t)fixture.declared);
}
int esp_http_client_close(void *client) { fixture.closed++; return 0; }
int esp_http_client_cleanup(void *client) { fixture.cleaned++; return 0; }
int esp_read_mac(uint8_t *mac, int type) { memcpy(mac, "\x00\x11\x22\x33\x44\x55", 6); return 0; }
const esp_app_desc_t *esp_app_get_description(void) {
    static const esp_app_desc_t desc = {.version = "test-version"}; return &desc;
}
const char *platform_device_slug(void) { return "test-device"; }
void *heap_caps_calloc(size_t count, size_t size, unsigned caps) {
    if (caps & MALLOC_CAP_SPIRAM) fixture.psram_allocations++;
    else fixture.internal_allocations++;
    if (fixture.no_memory || (fixture.no_psram && (caps & MALLOC_CAP_SPIRAM))) return NULL;
    return calloc(count, size);
}
void *heap_caps_realloc(void *ptr, size_t size, unsigned caps) {
    if (fixture.fail_grow || fixture.no_memory || (fixture.no_psram && (caps & MALLOC_CAP_SPIRAM))) return NULL;
    return realloc(ptr, size);
}
/* Exercise real DEFLATE and CRC on the host; only the SDK adapter is substituted. */
size_t tinfl_decompress_mem_to_mem(void *out, size_t out_len, const void *in, size_t in_len, int flags) {
    z_stream stream = {.next_in=(void *)in, .avail_in=in_len, .next_out=out, .avail_out=out_len};
    assert(inflateInit2(&stream, -15) == Z_OK);
    int rc = inflate(&stream, Z_FINISH);
    size_t n = stream.total_out;
    inflateEnd(&stream);
    return rc == Z_STREAM_END ? n : TINFL_DECOMPRESS_MEM_TO_MEM_FAILED;
}
static int consume(const void *data, size_t len, void *ctx) {
    fixture.callback_count++;
    if (ctx) return -1;
    return 0;
}
static void headers(bool image) {
    assert(!strcmp(fixture.id, "001122334455"));
    assert(!strcmp(fixture.version, "test-version"));
    assert(!strcmp(fixture.device, "test-device"));
    assert(!strcmp(fixture.connection, "close"));
    assert(!strcmp(fixture.accept_encoding, image && PLATFORM_HTTP_IMAGE_REQUEST_GZIP ? "gzip" : "identity"));
    assert(fixture.closed == 1 && fixture.cleaned == 1);
}
/* Run the same failures against JSON, image and stream entry points. */
static void expect_result(int expected) {
    for (int kind = 0; kind < 3; kind++) {
        fixture.offset = 0; fixture.closed = fixture.cleaned = 0;
        char *out = (void *)1;
        size_t length = 999;
        int rc = kind == 0 ? platform_http_get("http://bridge", &out, &length) :
                 kind == 1 ? platform_http_get_image("http://bridge", &out, &length) :
                 platform_http_stream("http://bridge", 4096, consume, NULL, &length);
        assert((rc == 0) == (expected == 0));
        if (rc != 0) { assert(length == 0); if (kind < 2) assert(out == NULL); }
        else { assert(length == fixture.size); if (kind < 2) { assert(!memcmp(out, fixture.data, length)); free(out); } }
        if (!fixture.init_failure) headers(kind == 1);
    }
}
static void test_protocol(void) {
    reset("{}", 2, 2); expect_result(0);
    reset("{}", 2, 0); expect_result(0); // complete chunked/close-delimited
    reset("{}", 2, 100); expect_result(-1);
    reset("{}", 2, 0); fixture.complete = false; expect_result(-1);
    reset("{}", 2, 2); fixture.complete = false; expect_result(-1);
    reset("{}", 2, 2); fixture.status = 500; expect_result(-1);
    fixture.status = 404; expect_result(-1);
    fixture.status = 302; expect_result(-1);
}
static void test_limits(void) {
    char *out; size_t n;
    reset("abcd", 4, 4);
    assert(!platform_http_get_bounded("x", 4, &out, &n)); assert(n == 4 && out[4] == 0); free(out);
    reset("abcd", 4, 0);
    assert(!platform_http_get_bounded("x", 4, &out, &n)); free(out);
    reset("abcde", 5, 0);
    assert(platform_http_get_bounded("x", 4, &out, &n) == -1 && !out && !n);
    reset("abcde", 5, 5);
    assert(platform_http_get_bounded("x", 4, &out, &n) == -1 && fixture.offset == 0);
    reset("abcde", 5, INT64_MAX); expect_result(-1);
    reset("", 0, 0);
    assert(!platform_http_get("x", &out, &n) && !n); free(out);
    fixture.status = 204;
    assert(!platform_http_post_json("x", "{}", &out, &n) && !n); free(out);
    reset("ok", 2, 2); fixture.status = 206;
    assert(!platform_http_get("x", &out, &n)); free(out);
    for (int flag = 0; flag < 2; flag++) {
        reset("", 0, 0);
        assert(platform_http_get_bounded("x", flag ? SIZE_MAX : 0, &out, &n) == -1 && !out && !n);
    }
    reset("ab\0d", 4, 0);
    assert(!platform_http_stream("x", 4, consume, NULL, &n) && n == 4);
    reset("abcde", 5, 0);
    assert(platform_http_stream("x", 4, consume, NULL, &n) == -1 && !n && !fixture.callback_count);
    reset("abcd", 4, 4);
    assert(platform_http_stream("x", 4, consume, (void *)1, &n) == -1 && !n);
}
static void test_failures_and_writes(void) {
    reset("{}", 2, 2); fixture.read_error = 1; expect_result(-1);
    reset("{}", 2, -1); expect_result(-1);
    reset("{}", 2, 2); fixture.encoding = "br"; expect_result(-1);
    reset("{}", 2, 2); fixture.encoding = " Identity \t"; expect_result(0);
    reset("{}", 2, 2); fixture.encoding = "identity"; fixture.duplicate_encoding = true; expect_result(-1);
    reset("{}", 2, 2); fixture.init_failure = true; expect_result(-1);
    reset("{}", 2, 2); fixture.open_failure = true; expect_result(-1);
    char *out; size_t n;
    reset("{}", 2, 2); fixture.write_limit = 2;
    assert(!platform_http_post_json("x", "{\"test\":true}", &out, &n)); free(out);
    assert(fixture.method == HTTP_METHOD_POST && fixture.writes == 7);
    assert(!strcmp(fixture.sent, "{\"test\":true}")); headers(false);
    for (int limit = -1; limit <= 0; limit++) {
        reset("{}", 2, 2); fixture.write_limit = limit;
        assert(platform_http_post_json("x", "{}", &out, &n) == -1 && !out && !n);
    }
    reset("{}", 2, 2); fixture.no_memory = true;
    assert(platform_http_get("x", &out, &n) == -1 && !out && !n);
    reset("{}", 2, 2); fixture.no_psram = true;
    int rc = platform_http_get("x", &out, &n);
    assert((rc == 0) == (PLATFORM_HTTP_JSON_INTERNAL_FALLBACK != 0));
    assert(fixture.internal_allocations == PLATFORM_HTTP_JSON_INTERNAL_FALLBACK); free(out);
    char large[5000] = {0};
    reset(large, sizeof(large), 0); fixture.fail_grow = true;
    assert(platform_http_get("x", &out, &n) == -1 && !out && !n);
    reset(large, sizeof(large), 0); fixture.no_psram = true;
    rc = platform_http_get("x", &out, &n);
    assert((rc == 0) == (PLATFORM_HTTP_JSON_INTERNAL_FALLBACK != 0)); free(out);
}
static void test_images(void) {
    char *out; size_t n;
    char *large = calloc(1, HTTP_IMAGE_MAX_BYTES + 1);
    assert(large);
    // Frame's packed artwork can be large; exact cap must work without spare read room.
    for (int known = 0; known < 2; known++) {
        reset(large, HTTP_IMAGE_MAX_BYTES, known ? HTTP_IMAGE_MAX_BYTES : 0);
        assert(!platform_http_get_image("x", &out, &n) && n == HTTP_IMAGE_MAX_BYTES); free(out);
        reset(large, HTTP_IMAGE_MAX_BYTES + 1, known ? HTTP_IMAGE_MAX_BYTES + 1 : 0);
        assert(platform_http_get_image("x", &out, &n) == -1 && !out && !n);
    }
    free(large);
    reset("", 0, 0); assert(platform_http_get_image("x", &out, &n) == -1 && !out && !n);
    unsigned char zipped[128];
    const char raw[] = "image\0bytes";
    z_stream stream = {.next_in=(void *)raw, .avail_in=sizeof(raw), .next_out=zipped, .avail_out=sizeof(zipped)};
    assert(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    assert(deflate(&stream, Z_FINISH) == Z_STREAM_END);
    size_t zlen = stream.total_out; deflateEnd(&stream);
    for (int header = 0; header < 2; header++) {
        reset(zipped, zlen, zlen); fixture.encoding = header ? "gzip" : NULL;
        assert(!platform_http_get_image("x", &out, &n) && n == sizeof(raw));
        assert(!memcmp(out, raw, n)); free(out);
    }
    reset(zipped, zlen, zlen); fixture.encoding = "gzip";
    assert(platform_http_get("x", &out, &n) == -1 && !out && !n);
    assert(platform_http_stream("x", 1024, consume, NULL, &n) == -1 && !n);
    unsigned char bad_header[128];
    memcpy(bad_header, zipped, zlen); bad_header[3] = 0xe0;
    reset(bad_header, zlen, zlen);
    assert(platform_http_get_image("x", &out, &n) == -1 && !out && !n);
    memcpy(bad_header, zipped, zlen); bad_header[3] = 4; bad_header[10] = 255; bad_header[11] = 255;
    reset(bad_header, zlen, zlen);
    assert(platform_http_get_image("x", &out, &n) == -1 && !out && !n);
    // Valid optional header CRC, then a corrupt one.
    memcpy(bad_header, zipped, 10); bad_header[3] = 2;
    uint32_t hcrc = crc32(0, bad_header, 10);
    bad_header[10] = hcrc; bad_header[11] = hcrc >> 8;
    memcpy(bad_header + 12, zipped + 10, zlen - 10);
    reset(bad_header, zlen + 2, zlen + 2);
    assert(!platform_http_get_image("x", &out, &n) && n == sizeof(raw)); free(out);
    bad_header[10] ^= 1; reset(bad_header, zlen + 2, zlen + 2);
    assert(platform_http_get_image("x", &out, &n) == -1 && !out && !n);
    zipped[zlen-8] ^= 1; reset(zipped, zlen, zlen);
    assert(platform_http_get_image("x", &out, &n) == -1 && !out && !n); // CRC
    zipped[zlen-8] ^= 1; zipped[zlen-1] = 127; reset(zipped, zlen, zlen);
    assert(platform_http_get_image("x", &out, &n) == -1 && !out && !n); // inflated size cap
    reset("bad", 3, 3); fixture.encoding = "gzip";
    assert(platform_http_get_image("x", &out, &n) == -1 && !out && !n);
}
int main(void) {
    test_protocol(); test_limits(); test_failures_and_writes(); test_images();
    puts("shared ESP HTTP transport tests passed");
}
