/*
 * boot_crash.h - STM32 错误日志与崩溃记录（F1/F4 共用）
 *
 * 日志区为 BOOT_LOG_BASE 起的文本追加区，每条 "[unix] text\r\n"。
 * HardFault 时追加异常栈帧与通用寄存器、故障状态寄存器及栈快照。
 */

#ifndef BOOT_CRASH_H
#define BOOT_CRASH_H

#include <stdint.h>

/* 追加一行 "[unix] text\r\n"；空间不足时整区擦除后从头部重写 */
void boot_log_append(uint32_t unix_time, const char *text);

/* HardFault：crash+1 并写回状态，异常现场追加到错误日志 */
void boot_crash_log(const uint32_t *frame, uint32_t sp, uint32_t exc_return,
                          uint32_t unix_time);

#endif /* BOOT_CRASH_H */
