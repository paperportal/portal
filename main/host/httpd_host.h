#pragma once

#include <stdint.h>

#include "esp_http_server.h"

struct HttpdHostRequestInfo {
    int32_t req_id;
    int32_t method;
    int32_t content_len;
    const char *uri;
    httpd_req_t *req;
};

bool httpd_host_get_request_info(int32_t req_id, HttpdHostRequestInfo *out_info);
