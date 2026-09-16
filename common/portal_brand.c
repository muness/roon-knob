// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "portal_brand.h"

#include <stdio.h>

#ifdef ESP_PLATFORM
#include <esp_app_desc.h>
#endif

const char PORTAL_BRAND_CSS[] =
    ":root{--pb-bg:#071a2e;--pb-surface:#0d2943;--pb-text:#f8fafb;"
    "--pb-muted:#b9c9d4;--pb-accent:#00c8f0;--pb-soft:#9cefff;"
    "--pb-attention:#ff654a;--pb-ok:#10b981;--pb-warn:#f59e0b;"
    "--pb-err:#ef4444;--pb-line:rgba(185,201,212,.3);--pb-on-accent:#04222f}"
    "@media(prefers-color-scheme:light){:root{--pb-bg:#f8fafb;"
    "--pb-surface:#ffffff;--pb-text:#172536;--pb-muted:#344b5f;"
    "--pb-accent:#00677d;--pb-line:#cdd8df;--pb-on-accent:#ffffff}}"
    "*{box-sizing:border-box}"
    "body{margin:0;padding:20px;background:var(--pb-bg);color:var(--pb-text);"
    "font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;"
    "font-size:16px;line-height:1.5}"
    "main{max-width:480px;margin:0 auto}"
    "h1,h2,h3{font-weight:600;line-height:1.25;margin:0 0 8px}"
    "h1{font-size:22px}h2{font-size:17px;margin-top:24px}h3{font-size:15px}"
    "p{margin:8px 0}"
    "a{color:var(--pb-accent)}"
    ".info,.hint,.lede,.empty,.technical{color:var(--pb-muted)}"
    ".hint{font-size:13px}"
    "nav{margin:10px 0 20px}nav a{margin-right:15px;text-decoration:none}"
    /* surfaces */
    "form,.card,.note,.saved,.next,.current,.connection,.device,.wifi-entry,"
    ".zone{background:var(--pb-surface);border:1px solid var(--pb-line);"
    "border-radius:12px;padding:14px 16px;margin:12px 0;max-width:480px}"
    ".section{max-width:480px}"
    ".note{font-size:14px}"
    ".current{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;"
    "font-size:14px}"
    ".device,.wifi-entry,.zone,.connection{display:flex;gap:12px;"
    "justify-content:space-between;align-items:center}"
    ".zone{cursor:pointer}.zone.active{border-color:var(--pb-accent)}"
    /* rows carry inline forms, which must stay invisible */
    ".device form,.wifi-entry form,.zone form,.actions form{display:inline;"
    "background:none;border:0;padding:0;margin:0;max-width:none}"
    ".actions{display:flex;flex-wrap:wrap;gap:8px;margin:16px 0}"
    /* form controls */
    "label{display:block;margin:14px 0 5px;color:var(--pb-muted);font-size:14px}"
    "input[type=text],input[type=password],input[type=url],select,textarea{"
    "display:block;width:100%;font:inherit;padding:10px;"
    "border:1px solid var(--pb-line);border-radius:8px;background:var(--pb-bg);"
    "color:var(--pb-text)}"
    "input:focus,select:focus,textarea:focus{border-color:var(--pb-accent)}"
    "a:focus-visible,button:focus-visible,input:focus-visible,"
    "select:focus-visible,summary:focus-visible{"
    "outline:3px solid var(--pb-accent);outline-offset:2px}"
    "input[type=submit],button,.btn{display:inline-block;font:inherit;"
    "font-weight:600;padding:12px 18px;margin-top:16px;border:0;"
    "border-radius:8px;background:var(--pb-accent);color:var(--pb-on-accent);"
    "cursor:pointer}"
    "input[type=submit]:hover,button:hover,.btn:hover{"
    "background:var(--pb-soft);color:#04222f}"
    "button:disabled{background:var(--pb-line);color:var(--pb-muted);"
    "cursor:wait}"
    ".btn-clear,.btn-danger,.danger,.btn-rm,input[type=submit].btn-clear,"
    "input[type=submit].btn-danger,button.btn-danger,button.danger,"
    "a.btn-rm,button.btn-rm{background:var(--pb-attention);color:#1b0a06}"
    ".btn-rm,a.btn-rm,button.btn-rm{text-decoration:none;font-size:13px;"
    "padding:7px 10px;margin-top:0}"
    ".btn-sm,input[type=submit].btn-sm,button.btn-sm{padding:6px 12px;"
    "font-size:13px;margin-top:0}"
    /* status pills */
    ".status,.success,.error,.warn{display:block;padding:10px 12px;"
    "margin:12px 0;max-width:480px;border-radius:8px;"
    "border:1px solid var(--pb-line);border-left:4px solid var(--pb-muted);"
    "background:var(--pb-surface);color:var(--pb-text)}"
    ".success,.status-ok{border-left-color:var(--pb-ok)}"
    ".warn,.status-warn{border-left-color:var(--pb-warn)}"
    ".error,.status-err{border-left-color:var(--pb-err)}"
    /* disclosure */
    "details{margin:18px 0;color:var(--pb-muted)}"
    "summary{cursor:pointer;color:var(--pb-accent);font-weight:600}"
    ".technical{font-size:14px}"
    /* tables (power debug) */
    "table{border-collapse:collapse;max-width:720px;width:100%}"
    "td,th{border-bottom:1px solid var(--pb-line);padding:7px;text-align:left}"
    "th{color:var(--pb-muted);font-weight:600}"
    "code{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;"
    "color:var(--pb-accent)}"
    /* brand header and footer */
    ".pb-head{display:flex;flex-wrap:wrap;align-items:baseline;gap:8px;"
    "padding-bottom:10px;margin-bottom:16px;"
    "border-bottom:1px solid var(--pb-line)}"
    ".pb-mark{font-size:22px;font-weight:700;letter-spacing:-.01em;"
    "color:var(--pb-text)}"
    ".pb-by{font-size:12px;color:var(--pb-muted)}"
    ".pb-product{margin-left:auto;font-size:15px;font-weight:600;"
    "color:var(--pb-accent)}"
    ".pb-foot{margin-top:32px;padding-top:12px;"
    "border-top:1px solid var(--pb-line);font-size:12px;"
    "color:var(--pb-muted);line-height:1.6}"
    ".pb-foot a{color:var(--pb-accent)}";

const char PORTAL_BRAND_HEADER_FMT[] =
    "<header class='pb-head'><span class='pb-mark'>HiPhi</span>"
    "<span class='pb-by'>by Open Horizon Labs</span>"
    "<span class='pb-product'>%s</span></header>";

const char PORTAL_BRAND_FOOTER_FMT[] =
    "<footer class='pb-foot'>HiPhi %s &middot; firmware %s &middot; "
    "HiPhi by Open Horizon Labs &middot; "
    "<a href='" PORTAL_BRAND_SITE_URL "'>Free for personal, noncommercial "
    "use; commercial use needs a license</a></footer>";

const char *portal_brand_firmware_version(void) {
#ifdef ESP_PLATFORM
    const esp_app_desc_t *desc = esp_app_get_description();
    if (desc && desc->version[0]) {
        return desc->version;
    }
#endif
    return "unknown";
}

const char *portal_brand_header_html(void) {
    static char header[192];
    if (!header[0]) {
        snprintf(header, sizeof(header), PORTAL_BRAND_HEADER_FMT,
                 PORTAL_BRAND_PRODUCT);
    }
    return header;
}

const char *portal_brand_footer_html(void) {
    static char footer[384];
    if (!footer[0]) {
        snprintf(footer, sizeof(footer), PORTAL_BRAND_FOOTER_FMT,
                 PORTAL_BRAND_PRODUCT, portal_brand_firmware_version());
    }
    return footer;
}
