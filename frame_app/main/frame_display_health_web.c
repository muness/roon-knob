// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "frame_display_health_web.h"
#include "frame_display_health.h"
#include "portal_brand.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
static esp_err_t display_health_handler(httpd_req_t *req) {
  char query[32] = {0}, format[8] = {0};
  bool json = httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
              httpd_query_key_value(query, "format", format, sizeof(format)) == ESP_OK &&
              strcmp(format, "json") == 0;
  if (json) {
    frame_display_health_t h;
    frame_display_health_snapshot(&h);
    cJSON *root = cJSON_CreateObject();
    if (!root) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory"); return ESP_FAIL; }
    cJSON_AddStringToObject(root, "attempted", h.attempted);
    cJSON_AddStringToObject(root, "downloaded", h.downloaded);
    cJSON_AddStringToObject(root, "displayed", h.displayed);
    cJSON_AddStringToObject(root, "phase", h.phase);
    cJSON_AddStringToObject(root, "error", h.error);
    cJSON_AddBoolToObject(root, "power_verified", h.power_verified);
    cJSON_AddBoolToObject(root, "busy", h.busy);
    cJSON_AddNumberToObject(root, "attempts", h.attempts);
    cJSON_AddNumberToObject(root, "completed", h.completed);
    cJSON_AddNumberToObject(root, "failures", h.failures);
    cJSON_AddNumberToObject(root, "last_completed_uptime_ms", h.completed_ms);
    cJSON_AddNumberToObject(root, "duration_ms", h.duration_ms);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory"); return ESP_FAIL; }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, body); free(body); return ESP_OK;
  }
  httpd_resp_set_type(req, "text/html");
  httpd_resp_sendstr_chunk(req, "<!doctype html><html lang='en'><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Frame display status</title><style>");
  httpd_resp_sendstr_chunk(req, PORTAL_BRAND_CSS);
  httpd_resp_sendstr_chunk(req, "td{overflow-wrap:anywhere;max-width:260px}</style></head><body><main>");
  httpd_resp_sendstr_chunk(req, portal_brand_header_html());
  httpd_resp_sendstr_chunk(req,
    "<nav><a href='/settings'>Settings</a><a href='/power-debug'>Power and crash report</a></nav>"
    "<h1>Display status</h1><p id='status' role='status' aria-live='polite'>Checking display...</p>"
    "<p>Downloading artwork and completing a panel refresh are separate steps. These records cover this boot.</p>"
    "<table><tbody><tr><th scope='row'>Panel rail settings</th><td id='power'></td></tr>"
    "<tr><th scope='row'>Refresh phase</th><td id='phase'></td></tr>"
    "<tr><th scope='row'>Last completed refresh</th><td id='completed'></td></tr>"
    "<tr><th scope='row'>Refresh duration</th><td id='duration'></td></tr>"
    "<tr><th scope='row'>Failure</th><td id='error'></td></tr></tbody></table>"
    "<details><summary>Artwork identities</summary><p>Requested: <code id='attempted'></code></p>"
    "<p>Downloaded: <code id='downloaded'></code></p><p>Last refreshed: <code id='displayed'></code></p></details>"
    "<p><a href='/display-health?format=json'>Export display report</a></p>"
    "<script>let pending=false;async function refresh(){if(pending||document.hidden)return;pending=true;"
    "try{const r=await fetch('/display-health?format=json',{cache:'no-store',signal:AbortSignal.timeout(8000)});"
    "if(!r.ok)throw Error();const h=await r.json();const put=(id,v)=>document.getElementById(id).textContent=v;"
    "put('status',h.busy?'Refreshing the panel...':h.error?'Refresh failed. Check power and the failure below.':h.completed?'Last refresh completed.':'No completed refresh recorded this boot.');"
    "put('power',h.power_verified?'Verified in PMIC':'Not verified');put('phase',h.phase||'Not started');"
    "put('completed',h.completed?h.completed+' completed; last at '+Math.round(h.last_completed_uptime_ms/1000)+' seconds after boot':'None this boot');"
    "put('duration',h.completed?(h.duration_ms/1000).toFixed(1)+' seconds':'Not available');"
    "for(const id of ['error','attempted','downloaded','displayed'])put(id,h[id]||'None');"
    "}catch(e){document.getElementById('status').textContent='Device unavailable. Check its connection and reload.';}finally{pending=false;}}"
    "refresh();setInterval(refresh,5000);</script>");
  httpd_resp_sendstr_chunk(req, portal_brand_footer_html());
  httpd_resp_sendstr_chunk(req, "</main></body></html>");
  httpd_resp_send_chunk(req, NULL, 0); return ESP_OK;
}

bool frame_display_health_web_register(httpd_handle_t server) {
    const httpd_uri_t route = {.uri = "/display-health", .method = HTTP_GET, .handler = display_health_handler};
    return httpd_register_uri_handler(server, &route) == ESP_OK;
}
