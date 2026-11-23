/*
 * config.c - 配置存储抽象层
 *
 * 只做参数检查并转发到端口层 port_config_xxx。
 * 未提供端口层实现的构建使用下面的 weak 默认值。
 */

#include "config.h"

#include <stddef.h>

#if defined(__GNUC__)
#define CONFIG_WEAK __attribute__((weak))
#else
#define CONFIG_WEAK
#endif

CONFIG_WEAK int port_config_read(void *buf, uint32_t max, uint32_t *len)
{
    (void)buf;
    (void)max;
    (void)len;
    return CONFIG_ERR;
}

CONFIG_WEAK int port_config_write(const void *data, uint32_t len)
{
    (void)data;
    (void)len;
    return CONFIG_ERR;
}

CONFIG_WEAK int port_config_erase(void)
{
    return CONFIG_ERR;
}

CONFIG_WEAK bool port_config_valid(void)
{
    return false;
}

int config_read(void *buf, uint32_t max, uint32_t *len)
{
    if (buf == NULL) {
        return CONFIG_ERR;
    }
    return port_config_read(buf, max, len);
}

int config_write(const void *data, uint32_t len)
{
    if (data == NULL && len != 0U) {
        return CONFIG_ERR;
    }
    return port_config_write(data, len);
}

int config_erase(void)
{
    return port_config_erase();
}

bool config_is_valid(void)
{
    return port_config_valid();
}
