#ifndef CONFIG_CMD_H
#define CONFIG_CMD_H

/*
 * config_cmd.h - 配置管理 Shell 命令
 *
 * 命令：
 *   cfg                     列出已注册的配置项（名称 / 键 / 大小 / 状态）
 *   cfg get <name>          读取配置并以 JSON 输出（无记录时输出默认值）
 *   cfg set <name> <json>   按 JSON 解析并保存（缺失字段回落默认值）
 *   cfg del <name>          删除该配置记录
 *
 * 配置项须先用 config.h 的 CFG_REGISTER(id, name) 注册。
 * 命令只读写 KV，运行中的结构体（如 app_config_t）在下次启动时生效。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 注册 cfg 命令（幂等）；返回 0 成功 */
int config_cmd_register(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_CMD_H */
