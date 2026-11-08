/*
 * PWM 风扇模块实现。
 *
 * 职责：
 *   把上层给出的 0..1000 档位（对应 0..100% 占空比）转发给底层 PWM，
 *   并提供开/关两个语义化接口。
 *
 * 数据流：
 *   init() 绑定 set_duty 回调并立即关断 -> set_speed() 限幅后输出，
 *   同时缓存当前档位；on()/off() 分别等价于 1000/0 档。
 *
 * 依赖与边界：
 *   - 仅依赖 标准头文件 与日志宏，无静态状态；
 *   - 档位上限硬编码为 1000，超过时自动钳位（不会报错）。
 */
#include "fan.h"
#include "log.h"

/*
 * 初始化风扇。
 * 参数：fan 对象指针（非 NULL）；ops 操作表（set_duty 必须实现）。
 * 返回：fan_off() 的返回值；参数非法返回 -1。
 * 说明：初始化成功后会立即以 0 档输出，保证上电默认停转。
 */
int fan_init(fan_t *fan, const fan_ops_t *ops)
{
    if (fan == NULL || ops == NULL || ops->set_duty == NULL) {
        return -1;
    }
    fan->ops = ops;
    fan->duty = 0;
    return fan_off(fan);
}

/*
 * 设置风扇档位。
 * 参数：fan 对象指针；duty 目标档位（0..1000，超出按 1000 处理）。
 * 返回：底层 set_duty 的返回值；fan 为 NULL 返回 -1。
 */
int fan_set_speed(fan_t *fan, uint16_t duty)
{
    if (fan == NULL) {
        return -1;
    }
    /* 上限钳位：1000 对应 100% 占空比 */
    if (duty > 1000) {
        duty = 1000;
    }
    fan->duty = duty;
    return fan->ops->set_duty(fan->ops->ctx, duty);
}

/*
 * 全速开启风扇。
 * 参数：fan 对象指针。
 * 返回：fan_set_speed(fan, 1000) 的返回值；fan 为 NULL 返回 -1。
 */
int fan_on(fan_t *fan)
{
    if (fan == NULL) {
        return -1;
    }
    LOG_I("fan", "on");
    return fan_set_speed(fan, 1000);
}

/*
 * 关闭风扇。
 * 参数：fan 对象指针。
 * 返回：fan_set_speed(fan, 0) 的返回值；fan 为 NULL 返回 -1。
 */
int fan_off(fan_t *fan)
{
    if (fan == NULL) {
        return -1;
    }
    return fan_set_speed(fan, 0);
}