#ifndef IOT_OS_H
#define IOT_OS_H

/*
 * 系统级接口：时基、延时、中断开关、复位与低功耗。
 *
 * 本文件只声明与芯片无关的最小系统原语，供 drivers / src / modules 使用；
 * 具体实现位于 platforms/<platform>/iot_<platform>.c：
 *   - simulator      : 纯软件推进 tick；复位/睡眠仅置标记，便于单元测试；
 *   - STM32F1/F4/H7  : HAL_GetTick / HAL_Delay / NVIC_SystemReset / PWR 低功耗；
 *   - AVR            : 定时器毫秒计数、_delay_ms/us、看门狗复位、sleep 模式。
 *
 * 约定：
 *   - tick 为 32 位毫秒计数，允许自然回绕；判断超时应使用无符号差值，
 *     例如 (uint32_t)(iot_tick_get() - start) >= timeout；
 *   - 延时为阻塞/忙等实现，禁止在中断上下文调用长延时；
 *   - iot_system_reset() 不返回；
 *   - iot_irq_disable()/iot_irq_enable() 不具备嵌套计数能力，
 *     只用于保护极短的临界区，进入与退出必须成对出现。
 */

#include "iot_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 获取系统毫秒时基。
 * 返回 : 自系统启动以来的毫秒数，允许回绕。
 * 注意 : 可在中断中调用；返回值只保证单调递增（回绕除外）。
 */
uint32_t iot_tick_get(void);

/*
 * 毫秒级阻塞延时。
 * ms   : 延时长度（毫秒），为 0 时立即返回。
 * 注意 : 依赖系统时基，调度器运行后精度受 tick 中断频率限制；
 *        不要在中断中调用。
 */
void iot_delay_ms(uint32_t ms);

/*
 * 微秒级阻塞延时。
 * us   : 延时长度（微秒），为 0 时立即返回。
 * 注意 : 通常为忙等实现，存在函数调用与循环开销，精度低于硬件定时器；
 *        用于 GPIO/单总线等短时序，不适合长延时。
 */
void iot_delay_us(uint32_t us);

/*
 * 使能全局中断。
 * 注意 : 与 iot_irq_disable() 成对使用，不支持嵌套计数。
 */
void iot_irq_enable(void);

/*
 * 关闭全局中断。
 * 注意 : 临界区应尽量短；与 iot_irq_enable() 成对使用，不支持嵌套计数。
 */
void iot_irq_disable(void);

/*
 * 软件复位系统。
 * 注意 : 该函数不返回，调用前应保存需要跨复位保留的数据。
 */
void iot_system_reset(void);

/*
 * 进入浅睡眠（IDLE/SLEEP）模式。
 * 说明 : 内核停止，外设与时钟保持运行，任意中断均可唤醒。
 * 返回 : 被唤醒后返回。
 */
void iot_system_sleep(void);

/*
 * 进入深度睡眠（STOP/PWR_DOWN）模式。
 * 说明 : 关闭大部分时钟以降低功耗，唤醒源由平台/芯片决定。
 * 返回 : 被唤醒后返回。
 * 注意 : STM32 的 STOP 模式唤醒后系统时钟可能退回内部时钟，
 *        AVR 的 PWR_DOWN 仅保留部分唤醒源，调用者需按平台恢复时钟与外设。
 */
void iot_system_deepsleep(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_OS_H */
