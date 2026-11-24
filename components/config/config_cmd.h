#ifndef CONFIG_CMD_H
#define CONFIG_CMD_H

/*
 * config_cmd.h - 配置存储通用 Shell 命令（cfg）
 *
 * 把“展示/保存/恢复默认/格式化/写读测试”封装成与具体配置结构体无关的命令：
 * 调用者只需提供存储句柄、配置内存、默认值与打印回调。
 *
 * 用法（板级/应用侧）：
 *   static app_config_t cfg;
 *   static uint8_t buf[sizeof(cfg) * 2];
 *   config_cmd_desc_t desc = {
 *       .store = boot_config_storage(),
 *       .data = &cfg, .size = sizeof(cfg), .scratch = buf,
 *       .set_defaults = my_defaults, .show = my_show, .fill_test = my_fill
 *   };
 *   config_cmd_register(&desc);      // 注册 cfg 命令
 *
 * 命令：cfg [show|save|reset|format|test [n]]
 *   test 连续写入 n 条（默认 8，约 372 条触发一次扇区轮转）并逐条回读比较，
 *   结束后恢复测试前的配置；结果与 Flash 状态打印到 Shell。
 *
 * 注意：本文件依赖 roshell，不编入 Bootloader（见 components/config/xmake.lua）。
 */

#include "config.h"

#include <stdint.h>

typedef struct {
    config_store_t *store;      /* 底层存储（必填） */
    void     *data;             /* 配置结构体内存（必填） */
    uint32_t  size;             /* 结构体大小（必填） */
    void     *scratch;          /* 测试缓冲，至少 2×size 字节（test 需要） */

    /* 填充默认值（reset/format 需要） */
    void (*set_defaults)(void *data);

    /* 打印当前配置（show/save 使用，可为 NULL） */
    void (*show)(const void *data);

    /* 生成第 round 轮测试内容（test 使用；为 NULL 时重复写入原值） */
    void (*fill_test)(void *data, uint32_t round);
} config_cmd_desc_t;

/* 注册 cfg 命令；返回 0 成功。descriptor 由内部拷贝，无需保持生命周期 */
int config_cmd_register(const config_cmd_desc_t *desc);

#endif /* CONFIG_CMD_H */
