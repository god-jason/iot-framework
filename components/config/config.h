#ifndef CONFIG_H
#define CONFIG_H

/*
 * config.h - 配置存储抽象接口
 *
 * config_xxx 直接调用端口层 port_config_xxx：
 *   config_read     -> port_config_read
 *   config_write    -> port_config_write
 *   config_erase    -> port_config_erase
 *   config_is_valid -> port_config_valid
 *
 * 板级实现在 platforms/stm32/stm32_config.c 中完成（双区备份 + CRC 校验）；
 * 未提供板级实现的构建使用 config.c 里的 weak 默认值（恒返回失败）。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_OK   0
#define CONFIG_ERR  (-1)

/* 读取当前配置到 buf（最多 max 字节）；len 返回实际长度，可为 NULL */
int config_read(void *buf, uint32_t max, uint32_t *len);

/* 保存配置；板级实现负责主备双区写入 */
int config_write(const void *data, uint32_t len);

/* 擦除主区和备份区 */
int config_erase(void);

/* 是否存在有效配置 */
bool config_is_valid(void);

/* ==========================================================================
 * 板级后端（platforms/stm32/stm32_config.c 实现）
 * ========================================================================== */

int  port_config_read(void *buf, uint32_t max, uint32_t *len);
int  port_config_write(const void *data, uint32_t len);
int  port_config_erase(void);
bool port_config_valid(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
