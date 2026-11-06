#include "iot_component.h"
#include "iot_string.h"

#define IOT_COMPONENT_MAX 16

static iot_component_t *s_components[IOT_COMPONENT_MAX];
static uint16_t s_next_id = 0;

int iot_component_register(iot_component_t *component)
{
    int i;
    if (component == NULL || component->name == NULL) {
        return IOT_ERR_PARAM;
    }
    for (i = 0; i < IOT_COMPONENT_MAX; i++) {
        if (s_components[i] == NULL) {
            s_components[i] = component;
            break;
        }
    }
    if (i == IOT_COMPONENT_MAX) {
        return IOT_ERR_OVERFLOW;
    }
    component->id = s_next_id++;
    component->state = IOT_COMPONENT_REGISTERED;
    return IOT_OK;
}

int iot_component_unregister(const char *name)
{
    int i;
    if (name == NULL) {
        return IOT_ERR_PARAM;
    }
    for (i = 0; i < IOT_COMPONENT_MAX; i++) {
        if (s_components[i] != NULL &&
            iot_string_compare(s_components[i]->name, name) == 0) {
            s_components[i] = NULL;
            return IOT_OK;
        }
    }
    return IOT_ERR_NOT_FOUND;
}

iot_component_t *iot_component_find(const char *name)
{
    int i;
    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < IOT_COMPONENT_MAX; i++) {
        if (s_components[i] != NULL &&
            iot_string_compare(s_components[i]->name, name) == 0) {
            return s_components[i];
        }
    }
    return NULL;
}

iot_component_state_t iot_component_get_state(const char *name)
{
    iot_component_t *c = iot_component_find(name);
    return (c == NULL) ? IOT_COMPONENT_NONE : c->state;
}

int iot_component_init_all(void)
{
    int i;
    for (i = 0; i < IOT_COMPONENT_MAX; i++) {
        iot_component_t *c = s_components[i];
        if (c == NULL) {
            continue;
        }
        if (c->init != NULL) {
            if (c->init(c) == IOT_OK) {
                c->state = IOT_COMPONENT_INITIALIZED;
            } else {
                c->state = IOT_COMPONENT_ERROR;
            }
        } else {
            c->state = IOT_COMPONENT_INITIALIZED;
        }
    }
    return IOT_OK;
}

int iot_component_start_all(void)
{
    int i;
    for (i = 0; i < IOT_COMPONENT_MAX; i++) {
        iot_component_t *c = s_components[i];
        if (c == NULL) {
            continue;
        }
        if (c->start != NULL) {
            if (c->start(c) == IOT_OK) {
                c->state = IOT_COMPONENT_STARTED;
            } else {
                c->state = IOT_COMPONENT_ERROR;
            }
        } else {
            c->state = IOT_COMPONENT_STARTED;
        }
    }
    return IOT_OK;
}

int iot_component_process_all(void)
{
    int i;
    for (i = 0; i < IOT_COMPONENT_MAX; i++) {
        iot_component_t *c = s_components[i];
        if (c != NULL && c->process != NULL && c->state == IOT_COMPONENT_STARTED) {
            c->process(c);
        }
    }
    return IOT_OK;
}

int iot_component_stop_all(void)
{
    int i;
    for (i = 0; i < IOT_COMPONENT_MAX; i++) {
        iot_component_t *c = s_components[i];
        if (c == NULL) {
            continue;
        }
        if (c->stop != NULL) {
            c->stop(c);
        }
        c->state = IOT_COMPONENT_STOPPED;
    }
    return IOT_OK;
}

int iot_component_deinit_all(void)
{
    int i;
    for (i = 0; i < IOT_COMPONENT_MAX; i++) {
        iot_component_t *c = s_components[i];
        if (c == NULL) {
            continue;
        }
        if (c->deinit != NULL) {
            c->deinit(c);
        }
        c->state = IOT_COMPONENT_NONE;
    }
    return IOT_OK;
}