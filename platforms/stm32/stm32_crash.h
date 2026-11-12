#ifndef STM32_CRASH_H
#define STM32_CRASH_H

/*
 * stm32_crash.h - Cortex-M 故障捕获入口
 *
 * 默认由本模块的 HardFault/MemManage/BusFault/UsageFault 处理函数调用。
 * 若应用自己实现了这些中断服务函数（CubeMX 的 stm32fxxx_it.c 常见），
 * 可把 ROS_CRASH_OWN_HANDLERS 置 0，并在自己的处理函数里转发：
 *
 *   void HardFault_Handler(void)
 *   {
 *       ros_crash_fault_entry(ros_crash_frame(), ROS_CRASH_HARD_FAULT, 0U);
 *   }
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 故障入口：采集现场 -> 记录 -> 打印 -> 按 ROS_CRASH_REBOOT 复位或停机 */
void ros_crash_fault_entry(uint32_t *frame, uint32_t reason,
                           uint32_t exc_return) __attribute__((noreturn));

/*
 * 取异常压栈帧指针：根据进入异常时 LR 的 bit2 选择 MSP/PSP。
 * 必须在异常处理函数内调用（此时 LR 仍保存 EXC_RETURN）。
 */
uint32_t *ros_crash_frame(void);

#ifdef __cplusplus
}
#endif

#endif /* STM32_CRASH_H */
