#pragma once
#include <stdio.h>
#include <stdbool.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define HTTP_GET 0
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
typedef int esp_err_t;
typedef void *httpd_handle_t;
typedef struct {FILE *out;bool json;} httpd_req_t;
typedef struct {const char *uri;int method;esp_err_t(*handler)(httpd_req_t*);} httpd_uri_t;
int httpd_req_get_url_query_str(httpd_req_t*,char*,size_t);
int httpd_query_key_value(const char*,const char*,char*,size_t);
int httpd_resp_sendstr_chunk(httpd_req_t*,const char*);
int httpd_resp_sendstr(httpd_req_t*,const char*);
int httpd_resp_send_chunk(httpd_req_t*,const char*,size_t);
int httpd_resp_send_err(httpd_req_t*,int,const char*);
int httpd_resp_set_type(httpd_req_t*,const char*);
int httpd_resp_set_hdr(httpd_req_t*,const char*,const char*);
int httpd_register_uri_handler(void*,const httpd_uri_t*);
