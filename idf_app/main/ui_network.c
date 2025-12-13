#include "ui_network.h"
#include "ui.h"

#include <esp_log.h>
#include <stdio.h>
#include <string.h>

#include "platform/platform_http.h"
#include "platform/platform_storage.h"
#include "wifi_manager.h"
#include "ota_update.h"
#include "controller_mode.h"

#if defined(__has_include)
#  if __has_include("lvgl.h")
#    include "lvgl.h"
#    define RK_UI_NETWORK_HAS_LVGL 1
#  else
#    define RK_UI_NETWORK_HAS_LVGL 0
#  endif
#else
#  define RK_UI_NETWORK_HAS_LVGL 0
#endif

static const char *TAG = "ui_network";

static void copy_str(char *dst, size_t len, const char *src) {
    if (!dst || len == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    size_t n = strnlen(src, len - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

#if RK_UI_NETWORK_HAS_LVGL

struct ui_net_widgets {
    lv_obj_t *panel;
    lv_obj_t *ssid_value;
    lv_obj_t *ip_value;
    lv_obj_t *version_label;
    lv_obj_t *status_label;
    lv_obj_t *wifi_form;
    lv_obj_t *wifi_ssid;
    lv_obj_t *wifi_pass;
    lv_obj_t *bridge_form;
    lv_obj_t *bridge_input;
};

static struct ui_net_widgets s_widgets;

static void refresh_labels(void) {
    rk_cfg_t cfg = {0};
    platform_storage_load(&cfg);

    if (s_widgets.ssid_value) {
        if (cfg.ssid[0]) {
            lv_label_set_text_fmt(s_widgets.ssid_value, "%s", cfg.ssid);
        } else {
            lv_label_set_text(s_widgets.ssid_value, "<unset>");
        }
    }
}

static void set_ip_text(const char *text) {
    if (!s_widgets.ip_value) {
        return;
    }
    lv_label_set_text_fmt(s_widgets.ip_value, "%s", (text && text[0]) ? text : "(no IP)");
}

static void set_status_text(const char *msg) {
    if (!s_widgets.status_label) {
        return;
    }
    lv_label_set_text_fmt(s_widgets.status_label, "%s", msg ? msg : "");
}

static void hide_form(lv_obj_t *form) {
    if (!form) {
        return;
    }
    lv_obj_add_flag(form, LV_OBJ_FLAG_HIDDEN);
}

static void show_form(lv_obj_t *form) {
    if (!form) {
        return;
    }
    lv_obj_clear_flag(form, LV_OBJ_FLAG_HIDDEN);
}

static void wifi_form_cancel(lv_event_t *e) {
    (void)e;
    hide_form(s_widgets.wifi_form);
}

static void bridge_form_cancel(lv_event_t *e) {
    (void)e;
    hide_form(s_widgets.bridge_form);
}

static void wifi_form_submit(lv_event_t *e) {
    (void)e;
    if (!s_widgets.wifi_ssid || !s_widgets.wifi_pass) {
        return;
    }
    rk_cfg_t cfg = {0};
    platform_storage_load(&cfg);
    copy_str(cfg.ssid, sizeof(cfg.ssid), lv_textarea_get_text(s_widgets.wifi_ssid));
    copy_str(cfg.pass, sizeof(cfg.pass), lv_textarea_get_text(s_widgets.wifi_pass));

    // Show feedback and close panel
    set_status_text("Connecting...");
    hide_form(s_widgets.wifi_form);
    if (s_widgets.panel) {
        lv_obj_add_flag(s_widgets.panel, LV_OBJ_FLAG_HIDDEN);
    }

    if (platform_storage_save(&cfg)) {
        wifi_mgr_reconnect(&cfg);
    } else {
        ESP_LOGW(TAG, "failed to save Wi-Fi config");
    }
}

static void bridge_form_submit(lv_event_t *e) {
    (void)e;
    if (!s_widgets.bridge_input) {
        return;
    }
    rk_cfg_t cfg = {0};
    platform_storage_load(&cfg);
    copy_str(cfg.bridge_base, sizeof(cfg.bridge_base), lv_textarea_get_text(s_widgets.bridge_input));
    if (!platform_storage_save(&cfg)) {
        ESP_LOGW(TAG, "failed to save bridge base");
    }
    hide_form(s_widgets.bridge_form);
}

static void forget_wifi_cb(lv_event_t *e) {
    (void)e;
    // Show feedback and hide settings panel
    set_status_text("Resetting WiFi...");
    if (s_widgets.panel) {
        lv_obj_add_flag(s_widgets.panel, LV_OBJ_FLAG_HIDDEN);
    }
    // Clear WiFi and trigger AP mode
    wifi_mgr_forget_wifi();
}

static void test_bridge_cb(lv_event_t *e) {
    (void)e;
    rk_cfg_t cfg = {0};
    platform_storage_load(&cfg);

    if (cfg.bridge_base[0] == '\0') {
        set_status_text("No bridge URL");
        return;
    }

    set_status_text("Testing...");

    char url[256];
    snprintf(url, sizeof(url), "%s/zones", cfg.bridge_base);

    char *response = NULL;
    size_t response_len = 0;
    int result = platform_http_get(url, &response, &response_len);
    platform_http_free(response);

    if (result == 0 && response_len > 0) {
        set_status_text("Bridge OK!");
        ESP_LOGI(TAG, "Bridge test passed: %s", cfg.bridge_base);
    } else {
        set_status_text("Bridge FAILED");
        ESP_LOGW(TAG, "Bridge test failed: %s (error %d)", cfg.bridge_base, result);
    }
}

static void hide_panel_cb(lv_event_t *e) {
    (void)e;
    if (s_widgets.panel) {
        lv_obj_add_flag(s_widgets.panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void bluetooth_mode_cb(lv_event_t *e) {
    (void)e;
    ESP_LOGI(TAG, "Switching to Bluetooth mode");

    // Hide settings panel
    if (s_widgets.panel) {
        lv_obj_add_flag(s_widgets.panel, LV_OBJ_FLAG_HIDDEN);
    }

    // Save Bluetooth as the selected zone
    rk_cfg_t cfg = {0};
    platform_storage_load(&cfg);
    strncpy(cfg.zone_id, "__bluetooth__", sizeof(cfg.zone_id) - 1);
    platform_storage_save(&cfg);

    // Switch to Bluetooth mode
    controller_mode_set(CONTROLLER_MODE_BLUETOOTH);
    ui_set_zone_name("Bluetooth");
}

static void clear_bridge_cb(lv_event_t *e) {
    (void)e;
    rk_cfg_t cfg = {0};
    platform_storage_load(&cfg);
    cfg.bridge_base[0] = '\0';  // Clear stored bridge URL
    if (platform_storage_save(&cfg)) {
        set_status_text("Bridge cleared (mDNS)");
        ESP_LOGI(TAG, "Bridge URL cleared, will use mDNS discovery");
    } else {
        set_status_text("Clear failed");
        ESP_LOGW(TAG, "Failed to clear bridge URL");
    }
}

static void update_version_label(void) {
    if (!s_widgets.version_label) return;

    const ota_info_t *info = ota_get_info();
    switch (info->status) {
        case OTA_STATUS_CHECKING:
            lv_label_set_text(s_widgets.version_label, "Checking...");
            break;
        case OTA_STATUS_AVAILABLE:
            lv_label_set_text_fmt(s_widgets.version_label, "v%s -> v%s",
                info->current_version, info->available_version);
            break;
        case OTA_STATUS_DOWNLOADING:
            lv_label_set_text_fmt(s_widgets.version_label, "Updating %d%%",
                info->progress_percent);
            break;
        case OTA_STATUS_UP_TO_DATE:
            lv_label_set_text_fmt(s_widgets.version_label, "v%s (latest)",
                info->current_version);
            break;
        case OTA_STATUS_ERROR:
            lv_label_set_text_fmt(s_widgets.version_label, "v%s (error)",
                info->current_version);
            break;
        default:
            lv_label_set_text_fmt(s_widgets.version_label, "v%s",
                info->current_version);
            break;
    }
}

static void check_update_cb(lv_event_t *e) {
    (void)e;
    const ota_info_t *info = ota_get_info();

    if (info->status == OTA_STATUS_AVAILABLE) {
        // Update is available - start it
        set_status_text("Starting update...");
        ota_start_update();
    } else {
        // Check for updates
        set_status_text("Checking...");
        ota_check_for_update();
    }
    update_version_label();
}

static void show_wifi_form(lv_event_t *e) {
    (void)e;
    if (!s_widgets.wifi_form) {
        return;
    }
    rk_cfg_t cfg = {0};
    platform_storage_load(&cfg);
    lv_textarea_set_text(s_widgets.wifi_ssid, cfg.ssid);
    lv_textarea_set_text(s_widgets.wifi_pass, cfg.pass);
    show_form(s_widgets.wifi_form);
    hide_form(s_widgets.bridge_form);
}

static void show_bridge_form(lv_event_t *e) {
    (void)e;
    if (!s_widgets.bridge_form) {
        return;
    }
    rk_cfg_t cfg = {0};
    platform_storage_load(&cfg);
    lv_textarea_set_text(s_widgets.bridge_input, cfg.bridge_base);
    show_form(s_widgets.bridge_form);
    hide_form(s_widgets.wifi_form);
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *label, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_center(lbl);
    return btn;
}

static lv_obj_t *create_text_area(lv_obj_t *parent, const char *placeholder, bool password) {
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_placeholder_text(ta, placeholder);
    if (password) {
        lv_textarea_set_password_mode(ta, true);
    }
    lv_obj_set_width(ta, lv_pct(100));
    return ta;
}

static void build_wifi_form(lv_obj_t *parent) {
    s_widgets.wifi_form = lv_obj_create(parent);
    lv_obj_set_width(s_widgets.wifi_form, lv_pct(100));
    lv_obj_set_flex_flow(s_widgets.wifi_form, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_widgets.wifi_form, 8, 0);
    lv_obj_add_flag(s_widgets.wifi_form, LV_OBJ_FLAG_HIDDEN);

    s_widgets.wifi_ssid = create_text_area(s_widgets.wifi_form, "SSID", false);
    s_widgets.wifi_pass = create_text_area(s_widgets.wifi_form, "Password", true);

    lv_obj_t *row = lv_obj_create(s_widgets.wifi_form);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cancel = create_button(row, "Cancel", wifi_form_cancel);
    lv_obj_t *save = create_button(row, "Save Wi-Fi", wifi_form_submit);
    lv_obj_set_flex_grow(cancel, 1);
    lv_obj_set_flex_grow(save, 1);
}

static void build_bridge_form(lv_obj_t *parent) {
    s_widgets.bridge_form = lv_obj_create(parent);
    lv_obj_set_width(s_widgets.bridge_form, lv_pct(100));
    lv_obj_set_flex_flow(s_widgets.bridge_form, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_widgets.bridge_form, 8, 0);
    lv_obj_add_flag(s_widgets.bridge_form, LV_OBJ_FLAG_HIDDEN);

    s_widgets.bridge_input = create_text_area(s_widgets.bridge_form, "Bridge Base URL", false);

    lv_obj_t *row = lv_obj_create(s_widgets.bridge_form);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cancel = create_button(row, "Cancel", bridge_form_cancel);
    lv_obj_t *save = create_button(row, "Save Bridge", bridge_form_submit);
    lv_obj_set_flex_grow(cancel, 1);
    lv_obj_set_flex_grow(save, 1);
}

static void ensure_panel(void) {
    if (s_widgets.panel) {
        return;
    }
    lv_obj_t *screen = lv_screen_active();
    s_widgets.panel = lv_obj_create(screen);
    lv_obj_set_size(s_widgets.panel, 220, 220);
    lv_obj_center(s_widgets.panel);
    lv_obj_set_flex_flow(s_widgets.panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_widgets.panel, 10, 0);

    lv_obj_t *title = lv_label_create(s_widgets.panel);
    lv_label_set_text(title, "Settings");

    // Version row
    lv_obj_t *ver_row = lv_obj_create(s_widgets.panel);
    lv_obj_set_size(ver_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ver_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(ver_row, 4, 0);
    lv_obj_clear_flag(ver_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_text(lv_label_create(ver_row), "Version:");
    s_widgets.version_label = lv_label_create(ver_row);
    lv_label_set_text_fmt(s_widgets.version_label, "v%s", ota_get_current_version());

    lv_obj_t *ssid_row = lv_obj_create(s_widgets.panel);
    lv_obj_set_size(ssid_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ssid_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(ssid_row, 4, 0);
    lv_obj_clear_flag(ssid_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_text(lv_label_create(ssid_row), "SSID:");
    s_widgets.ssid_value = lv_label_create(ssid_row);

    lv_obj_t *ip_row = lv_obj_create(s_widgets.panel);
    lv_obj_set_size(ip_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ip_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(ip_row, 4, 0);
    lv_obj_clear_flag(ip_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_text(lv_label_create(ip_row), "IP:");
    s_widgets.ip_value = lv_label_create(ip_row);

    s_widgets.status_label = lv_label_create(s_widgets.panel);
    lv_label_set_text(s_widgets.status_label, "Wi-Fi idle");

    create_button(s_widgets.panel, "Check for Update", check_update_cb);
    create_button(s_widgets.panel, "Test Bridge", test_bridge_cb);
    create_button(s_widgets.panel, "Clear Bridge", clear_bridge_cb);
    create_button(s_widgets.panel, "Forget Wi-Fi", forget_wifi_cb);
    create_button(s_widgets.panel, "Bluetooth Mode", bluetooth_mode_cb);
    create_button(s_widgets.panel, "Back", hide_panel_cb);
}

typedef struct {
    rk_net_evt_t evt;
    char ip[16];
} ui_net_evt_msg_t;

static void apply_evt_async(void *data) {
    ui_net_evt_msg_t *msg = data;
    char ssid[33] = {0};
    wifi_mgr_get_ssid(ssid, sizeof(ssid));

    switch (msg->evt) {
        case RK_NET_EVT_CONNECTING:
            set_status_text("Connecting…");
            set_ip_text("");
            // Show on main screen with SSID
            if (ssid[0]) {
                char buf[64];
                snprintf(buf, sizeof(buf), "WiFi: %s…", ssid);
                ui_set_network_status(buf);
            } else {
                ui_set_network_status("WiFi: Connecting…");
            }
            break;
        case RK_NET_EVT_GOT_IP:
            set_status_text("Online");
            set_ip_text(msg->ip);
            // Clear main screen status on successful connection
            ui_set_network_status(NULL);
            break;
        case RK_NET_EVT_FAIL:
            // Generic failure - show reason if provided
            if (msg->ip[0]) {
                set_status_text(msg->ip);  // ip field contains error string
                // Show on main screen
                char buf[80];
                snprintf(buf, sizeof(buf), "WiFi: %s", msg->ip);
                ui_set_network_status(buf);
            } else {
                set_status_text("Retrying…");
                ui_set_network_status("WiFi: Retrying…");
            }
            break;
        case RK_NET_EVT_WRONG_PASSWORD:
            set_status_text("Wrong password");
            ui_set_network_status("WiFi: Wrong password");
            break;
        case RK_NET_EVT_NO_AP_FOUND:
            set_status_text("Network not found");
            if (ssid[0]) {
                char buf[64];
                snprintf(buf, sizeof(buf), "WiFi: '%s' not found", ssid);
                ui_set_network_status(buf);
            } else {
                ui_set_network_status("WiFi: Network not found");
            }
            break;
        case RK_NET_EVT_AUTH_TIMEOUT:
            set_status_text("Auth timeout");
            ui_set_network_status("WiFi: Auth timeout");
            break;
        case RK_NET_EVT_AP_STARTED:
            set_status_text("Setup: roon-knob-setup");
            set_ip_text("192.168.4.1");
            // Show setup instructions on main screen
            ui_set_network_status("Setup: Connect to 'roon-knob-setup'");
            break;
        case RK_NET_EVT_AP_STOPPED:
            set_status_text("Connecting…");
            set_ip_text("");
            ui_set_network_status("WiFi: Connecting…");
            break;
        default:
            break;
    }
    lv_free(msg);
}

void ui_network_register_menu(void) {
    ensure_panel();
    refresh_labels();
    char ip[16] = {0};
    if (wifi_mgr_get_ip(ip, sizeof(ip))) {
        set_ip_text(ip);
    } else {
        set_ip_text("(no IP)");
    }
    set_status_text("Wi-Fi idle");
}

void ui_network_on_event(rk_net_evt_t evt, const char *ip_opt) {
    ui_net_evt_msg_t *msg = lv_malloc(sizeof(*msg));
    if (!msg) {
        ESP_LOGW(TAG, "no memory for UI event");
        return;
    }
    msg->evt = evt;
    copy_str(msg->ip, sizeof(msg->ip), ip_opt ?: "");
    lv_async_call(apply_evt_async, msg);
}

void ui_show_settings(void) {
    ensure_panel();
    refresh_labels();
    update_version_label();
    char ip[16] = {0};
    if (wifi_mgr_get_ip(ip, sizeof(ip))) {
        set_ip_text(ip);
    } else {
        set_ip_text("(no IP)");
    }
    set_status_text("Wi-Fi idle");
    lv_obj_clear_flag(s_widgets.panel, LV_OBJ_FLAG_HIDDEN);
}

void ui_hide_settings(void) {
    if (s_widgets.panel) {
        lv_obj_add_flag(s_widgets.panel, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ui_is_settings_visible(void) {
    return s_widgets.panel && !lv_obj_has_flag(s_widgets.panel, LV_OBJ_FLAG_HIDDEN);
}

#else

void ui_network_register_menu(void) {
    ESP_LOGI(TAG, "LVGL not available; network UI disabled");
}

void ui_network_on_event(rk_net_evt_t evt, const char *ip_opt) {
    (void)evt;
    (void)ip_opt;
}

void ui_show_settings(void) {
    ESP_LOGI(TAG, "Settings UI not available (no LVGL)");
}

void ui_hide_settings(void) {
}

bool ui_is_settings_visible(void) {
    return false;
}

#endif
