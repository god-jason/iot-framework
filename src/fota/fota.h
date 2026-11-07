#ifndef FOTA_H
#define FOTA_H

/*
 * FOTA 远程固件升级：下载固件 -> 写入升级分区 -> 校验 -> 请求重启升级。
 * 下载源通过数据源回调解耦（可接 HTTP / UART / 串口升级等）。
 *
 * 模块说明：
 *   - 本模块只负责流程编排，分区的读写与校验由 upgrade 模块完成；
 *   - 成功后默认请求 Bootloader 切换镜像，也可注册自定义 apply 钩子；
 *   - 模块级回调为静态变量，同一时刻只支持一个升级任务。
 * 使用约束：
 *   - fota_run 为阻塞式调用，需在具备足够栈空间的任务中执行；
 *   - 运行前必须先 fota_init() 并设置读取回调。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 读取一块固件数据：返回该块字节数，0 表示结束，<0 表示错误 */
typedef int (*fota_read_fn)(uint8_t *buf, uint32_t max, void *user);

/* 应用升级钩子：请求重启进入 Bootloader（可为 NULL，使用默认实现） */
typedef void (*fota_apply_fn)(void);

/*
 * 初始化 FOTA 模块并清空回调。
 * 返回：底层 upgrade_init() 的返回值。
 */
int fota_init(void);

/*
 * 设置固件读取回调。
 * 参数：fn 读取函数（不可为 NULL）；user 透传用户指针。
 * 返回：0 成功；fn 为 NULL 返回 -1。
 */
int fota_set_read(fota_read_fn fn, void *user);

/*
 * 设置升级完成后的应用钩子。
 * 参数：fn 钩子函数，NULL 表示使用 boot_request_upgrade()。
 * 返回：恒为 0。
 */
int fota_set_apply(fota_apply_fn fn);

/*
 * 执行一次升级：下载 + 写入 + 校验 + 请求重启。
 * 参数：version 固件版本号；size 固件字节数（必须 > 0）。
 * 返回：0 成功；未设置读取源或任一步骤失败返回 -1。
 */
int fota_run(uint32_t version, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* FOTA_H */