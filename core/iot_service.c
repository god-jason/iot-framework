#include "iot_service.h"
#include "iot_string.h"

#define IOT_SERVICE_MAX 16

static iot_service_t *s_services[IOT_SERVICE_MAX];

iot_service_t *iot_service_find(const char *name)
{
    int i;
    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < IOT_SERVICE_MAX; i++) {
        if (s_services[i] != NULL &&
            iot_string_compare(s_services[i]->name, name) == 0) {
            return s_services[i];
        }
    }
    return NULL;
}

int iot_service_register(iot_service_t *service)
{
    int i;
    if (service == NULL || service->name == NULL) {
        return IOT_ERR_PARAM;
    }
    for (i = 0; i < IOT_SERVICE_MAX; i++) {
        if (s_services[i] == NULL) {
            s_services[i] = service;
            break;
        }
    }
    if (i == IOT_SERVICE_MAX) {
        return IOT_ERR_OVERFLOW;
    }
    service->enabled = 1;
    service->state = 0;
    if (service->init != NULL) {
        if (service->init(service->ctx) != IOT_OK) {
            return IOT_ERR_STATE;
        }
    }
    return IOT_OK;
}

int iot_service_unregister(const char *name)
{
    int i;
    if (name == NULL) {
        return IOT_ERR_PARAM;
    }
    for (i = 0; i < IOT_SERVICE_MAX; i++) {
        if (s_services[i] != NULL &&
            iot_string_compare(s_services[i]->name, name) == 0) {
            s_services[i] = NULL;
            return IOT_OK;
        }
    }
    return IOT_ERR_NOT_FOUND;
}

int iot_service_start(const char *name)
{
    iot_service_t *s = iot_service_find(name);
    if (s == NULL) {
        return IOT_ERR_NOT_FOUND;
    }
    if (s->start != NULL) {
        if (s->start(s->ctx) != IOT_OK) {
            return IOT_ERR_STATE;
        }
    }
    s->state = 1;
    s->enabled = 1;
    return IOT_OK;
}

int iot_service_stop(const char *name)
{
    iot_service_t *s = iot_service_find(name);
    if (s == NULL) {
        return IOT_ERR_NOT_FOUND;
    }
    if (s->stop != NULL) {
        s->stop(s->ctx);
    }
    s->state = 0;
    return IOT_OK;
}

int iot_service_start_all(void)
{
    int i;
    for (i = 0; i < IOT_SERVICE_MAX; i++) {
        if (s_services[i] != NULL && s_services[i]->enabled) {
            iot_service_start(s_services[i]->name);
        }
    }
    return IOT_OK;
}

int iot_service_stop_all(void)
{
    int i;
    for (i = 0; i < IOT_SERVICE_MAX; i++) {
        if (s_services[i] != NULL) {
            iot_service_stop(s_services[i]->name);
        }
    }
    return IOT_OK;
}

int iot_service_process_all(void)
{
    int i;
    for (i = 0; i < IOT_SERVICE_MAX; i++) {
        iot_service_t *s = s_services[i];
        if (s != NULL && s->state == 1 && s->process != NULL) {
            s->process(s->ctx);
        }
    }
    return IOT_OK;
}