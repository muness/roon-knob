#include "ui_network.h"
#include "ui.h"
#include "font_manager.h"

#include <esp_log.h>
#include <stdio.h>
#include <string.h>

#include "platform/platform_http.h"
#include "platform/platform_storage.h"
#include "wifi_manager.h"
#include "ota_update.h"
#include "controller_mode.h"
#include "roon_client.h"

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
    lv_obj_t *name_value;
    lv_obj_t *bridge_value;
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
static lv_obj_t *s_reset_confirm_dialog = NULL;

// Forward declarations
static void set_status_text(const char *msg);

static void do_factory_reset(void) {
    set_status_text("Factory Reset...");
    if (s_widgets.panel) {
        lv_obj_add_flag(s_widgets.panel, LV_OBJ_FLAG_HIDDEN);
    }
    wifi_mgr_forget_wifi();
}

static void reset_confirm_btn_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    bool confirmed = (bool)(intptr_t)lv_obj_get_user_data(btn);

    // Hide dialog
    if (s_reset_confirm_dialog) {
        lv_obj_delete(s_reset_confirm_dialog);
        s_reset_confirm_dialog = NULL;
    }

    if (confirmed) {
        do_factory_reset();
    }
}

static void show_reset_confirm_dialog(void) {
    if (s_reset_confirm_dialog) {
        return;  // Already visible
    }

    // Hide settings panel temporarily
    if (s_widgets.panel) {
        lv_obj_add_flag(s_widgets.panel, LV_OBJ_FLAG_HIDDEN);
    }

    // Create fullscreen dark overlay
    s_reset_confirm_dialog = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_reset_confirm_dialog, 360, 360);
    lv_obj_center(s_reset_confirm_dialog);
    lv_obj_set_style_bg_color(s_reset_confirm_dialog, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_reset_confirm_dialog, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_reset_confirm_dialog, 0, 0);
    lv_obj_set_style_radius(s_reset_confirm_dialog, 0, 0);
    lv_obj_set_style_pad_all(s_reset_confirm_dialog, 0, 0);

    // Title
    lv_obj_t *title = lv_label_create(s_reset_confirm_dialog);
    lv_label_set_text(title, "Factory Reset?");
    lv_obj_set_style_text_font(title, font_manager_get_small(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xfafafa), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    // Subtitle
    lv_obj_t *subtitle = lv_label_create(s_reset_confirm_dialog);
    lv_label_set_text(subtitle, "Erases all settings");
    lv_obj_set_style_text_font(subtitle, font_manager_get_small(), 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 100);

    // Reset button (left, red)
    lv_obj_t *btn_reset = lv_btn_create(s_reset_confirm_dialog);
    lv_obj_set_size(btn_reset, 110, 50);
    lv_obj_align(btn_reset, LV_ALIGN_CENTER, -60, 30);
    lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0xc62828), 0);  // Red
    lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0xe53935), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_reset, 10, 0);
    lv_obj_set_user_data(btn_reset, (void *)(intptr_t)true);
    lv_obj_add_event_cb(btn_reset, reset_confirm_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *reset_label = lv_label_create(btn_reset);
    lv_label_set_text(reset_label, "Reset");
    lv_obj_set_style_text_font(reset_label, font_manager_get_small(), 0);
    lv_obj_set_style_text_color(reset_label, lv_color_hex(0xfafafa), 0);
    lv_obj_center(reset_label);

    // Cancel button (right, gray)
    lv_obj_t *btn_cancel = lv_btn_create(s_reset_confirm_dialog);
    lv_obj_set_size(btn_cancel, 110, 50);
    lv_obj_align(btn_cancel, LV_ALIGN_CENTER, 60, 30);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x3c3c3c), 0);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x5a5a5a), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_cancel, 10, 0);
    lv_obj_set_user_data(btn_cancel, (void *)(intptr_t)false);
    lv_obj_add_event_cb(btn_cancel, reset_confirm_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel_label = lv_label_create(btn_cancel);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_font(cancel_label, font_manager_get_small(), 0);
    lv_obj_set_style_text_color(cancel_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_center(cancel_label);

    ESP_LOGI(TAG, "Factory reset confirmation dialog shown");
}

static void refresh_labels(void) {
    rk_cfg_t cfg = {0};
    platform_storage_load(&cfg);

    if (s_widgets.name_value) {
        if (cfg.knob_name[0]) {
            lv_label_set_text_fmt(s_widgets.name_value, "%s", cfg.knob_name);
        } else {
            lv_label_set_text(s_widgets.name_value, "<unset>");
        }
    }

    if (s_widgets.ssid_value) {
        if (cfg.ssid[0]) {
            lv_label_set_text_fmt(s_widgets.ssid_value, "%s", cfg.ssid);
        } else {
            lv_label_set_text(s_widgets.ssid_value, "<unset>");
        }
    }

    if (s_widgets.bridge_value) {
        char bridge_url[128];
        if (roon_client_get_bridge_url(bridge_url, sizeof(bridge_url))) {
            // Extract host:port from URL (skip http://)
            const char *host = bridge_url;
            if (strncmp(host, "http://", 7) == 0) {
                host += 7;
            }
            // Show with discovery method
            const char *source = roon_client_is_bridge_mdns() ? " (mDNS)" : "";
            lv_label_set_text_fmt(s_widgets.bridge_value, "%s%s", host, source);
        } else {
            lv_label_set_text(s_widgets.bridge_value, "(discovering...)");
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

static void factory_reset_cb(lv_event_t *e) {
    (void)e;
    show_reset_confirm_dialog();
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
        // Check for updates (force=true to bypass dev version skip)
        set_status_text("Checking...");
        ota_check_for_update(true);
    }
    update_version_label();
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

    // Name row (knob name from bridge config)
    lv_obj_t *name_row = lv_obj_create(s_widgets.panel);
    lv_obj_set_size(name_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(name_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(name_row, 4, 0);
    lv_obj_clear_flag(name_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_text(lv_label_create(name_row), "Name:");
    s_widgets.name_value = lv_label_create(name_row);

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

    // Bridge row (with horizontal scroll for long URLs)
    lv_obj_t *bridge_row = lv_obj_create(s_widgets.panel);
    lv_obj_set_size(bridge_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bridge_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(bridge_row, 4, 0);
    lv_obj_clear_flag(bridge_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_text(lv_label_create(bridge_row), "Bridge:");
    s_widgets.bridge_value = lv_label_create(bridge_row);
    lv_obj_set_width(s_widgets.bridge_value, 120);  // Constrain width to enable scroll
    lv_label_set_long_mode(s_widgets.bridge_value, LV_LABEL_LONG_SCROLL_CIRCULAR);

    s_widgets.status_label = lv_label_create(s_widgets.panel);
    lv_label_set_text(s_widgets.status_label, "Wi-Fi idle");

    create_button(s_widgets.panel, "Check for Update", check_update_cb);
    create_button(s_widgets.panel, "Test Bridge", test_bridge_cb);
    create_button(s_widgets.panel, "Factory Reset", factory_reset_cb);
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
            set_status_text("Connecting...");
            set_ip_text("");
            // Show on main screen with SSID
            if (ssid[0]) {
                char buf[64];
                snprintf(buf, sizeof(buf), "WiFi: %s...", ssid);
                ui_set_network_status(buf);
            } else {
                ui_set_network_status("WiFi: Connecting...");
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
                set_status_text("Retrying...");
                ui_set_network_status("WiFi: Retrying...");
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
            set_status_text("Connecting...");
            set_ip_text("");
            ui_set_network_status("WiFi: Connecting...");
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
