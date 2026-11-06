#ifndef IOT_ML307_HTTP_H
#define IOT_ML307_HTTP_H

/*
 * ML307 HTTP 原语（基于 iot_at 引擎，模组内置 HTTP）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_ml307_http_get(const char *url, const char **resp, uint32_t *resp_len);
int iot_ml307_http_post(const char *url, const char *body,
                        const char **resp, uint32_t *resp_len);

#ifdef __cplusplus
}
#endif

#endif /* IOT_ML307_HTTP_H */