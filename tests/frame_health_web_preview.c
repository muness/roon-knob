// Execute the actual maintenance-page handler with host HTTP doubles.
#include <stdio.h>
#include <string.h>
#include "frame_web_sdk.h"
#include "../frame_app/main/frame_display_health_web.c"
int httpd_req_get_url_query_str(httpd_req_t*r,char*b,size_t n){snprintf(b,n,"%s",r->json?"format=json":"");return 0;}
int httpd_query_key_value(const char*q,const char*k,char*b,size_t n){snprintf(b,n,"%s",strstr(q,"json")?"json":"");return 0;}
int httpd_resp_sendstr_chunk(httpd_req_t*r,const char*b){if(b)fputs(b,r->out);return 0;}
int httpd_resp_sendstr(httpd_req_t*r,const char*b){return httpd_resp_sendstr_chunk(r,b);}
int httpd_resp_send_chunk(httpd_req_t*r,const char*b,size_t n){if(b)fwrite(b,1,n,r->out);return 0;}
int httpd_resp_send_err(httpd_req_t*r,int e,const char*s){return -1;}
int httpd_resp_set_type(httpd_req_t*r,const char*s){return 0;}
int httpd_resp_set_hdr(httpd_req_t*r,const char*a,const char*b){return 0;}
int httpd_register_uri_handler(void*s,const httpd_uri_t*r){return 0;}
int main(int argc,char**argv){
 frame_display_health_attempt("art:12345678:1",2000);frame_display_health_downloaded("art:12345678:1");frame_display_health_power(true);frame_display_health_complete(22000);
 frame_display_health_attempt("art:87654321:2",24000);frame_display_health_phase("refresh");frame_display_health_fail("panel BUSY timeout");
 httpd_req_t r={stdout,argc>1&&!strcmp(argv[1],"json")};return display_health_handler(&r);
}
