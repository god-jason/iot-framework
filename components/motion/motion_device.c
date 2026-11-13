#include "motion_device.h"
#include "motion_error.h"
#include "ros.h"
#include "common.h"

/*
 * motion_device.c —— 设备操作抽象层实现。
 *
 * 模块职责：
 *   - 保存移植层注册的一份回调表（单例）；
 *   - 把语义化设备调用转发给回调，并统一错误码；
 *   - 用两个定长位图记录 STEP / PWM 设备的占用情况，供 WAIT ALL 判断。
 *
 * 数据流：motion_device_register() 拷贝 ops -> 各 motion_device_xxx()
 *         先取表并校验回调，再调用 -> call_result() 归一化返回值。
 *
 * 边界约束：
 *   - s_step_active / s_pwm_active 长度为 MOTION_MAX_DEVICES，
 *     note_start/note_stop 对越界 id 直接忽略；
 *   - 回调缺失一律返回 MOTION_ERR_DEVICE，不会解引用空指针；
 *   - 占用位图在首次注册时通过 ros_mem 分配，重复注册复用；
 *   - 全局状态非线程安全。
 */

#include <string.h>

static bool s_registered;                 /* 回调表是否已注册 */
static motion_device_ops_t s_ops;         /* 当前生效的回调表副本 */
static bool *s_step_active;               /* 步进设备占用位图（惰性分配） */
static bool *s_pwm_active;                /* PWM 设备占用位图（惰性分配） */

/* 惰性分配占用位图；已分配则复用，全部成功返回 0。 */
static int ensure_tables(void)
{
    if (s_step_active == NULL) {
        s_step_active = (bool *)ros_mem_calloc(MOTION_MAX_DEVICES,
                                            sizeof(*s_step_active));
        if (s_step_active == NULL) {
            return MOTION_ERR_MEMORY;
        }
    }
    if (s_pwm_active == NULL) {
        s_pwm_active = (bool *)ros_mem_calloc(MOTION_MAX_DEVICES,
                                           sizeof(*s_pwm_active));
        if (s_pwm_active == NULL) {
            return MOTION_ERR_MEMORY;
        }
    }
    return 0;
}

/* 注册设备回调表。
 * 参数：ops 回调表指针。
 * 返回：0 成功；MOTION_ERR_PARAMETER ops 为 NULL；
 *       MOTION_ERR_MEMORY 占用位图分配失败。
 * 说明：按值拷贝，调用方的结构体随后可以释放；注册会清空占用位图。 */
int motion_device_register(const motion_device_ops_t *ops)
{
    int rc;

    if (ops == NULL) {
        return MOTION_ERR_PARAMETER;
    }

    rc = ensure_tables();
    if (rc != 0) {
        return rc;
    }

    s_ops = *ops;
    s_registered = true;
    memset(s_step_active, 0, (size_t)MOTION_MAX_DEVICES *
                             sizeof(*s_step_active));
    memset(s_pwm_active, 0, (size_t)MOTION_MAX_DEVICES *
                            sizeof(*s_pwm_active));
    return 0;
}

/* 获取当前回调表；未注册时返回 NULL。 */
const motion_device_ops_t *motion_device_get(void)
{
    return s_registered ? &s_ops : NULL;
}

/* 复位设备层：注销回调表并清空占用位图。 */
void motion_device_reset(void)
{
    s_registered = false;
    memset(&s_ops, 0, sizeof(s_ops));
    if (s_step_active != NULL) {
        memset(s_step_active, 0, (size_t)MOTION_MAX_DEVICES *
                                 sizeof(*s_step_active));
    }
    if (s_pwm_active != NULL) {
        memset(s_pwm_active, 0, (size_t)MOTION_MAX_DEVICES *
                                sizeof(*s_pwm_active));
    }
}

/* 内部取表：未注册返回 NULL，供各封装函数做前置校验。 */
static const motion_device_ops_t *get_ops(void)
{
    return s_registered ? &s_ops : NULL;
}

/* 归一化回调返回值：0 视为成功，其余（含负数与正数）统一为执行失败。 */
static int call_result(int result)
{
    return (result == 0) ? 0 : MOTION_ERR_DEVICE_EXEC;
}

/* 启动步进运动并标记设备占用。
 * 返回：0 成功；MOTION_ERR_DEVICE 未注册或回调缺失；
 *       MOTION_ERR_DEVICE_EXEC 回调执行失败。 */
int motion_device_step_start(uint16_t id, uint8_t dir, double speed,
                             motion_unit_e unit, int32_t pulse,
                             uint32_t time_ms, uint32_t acc_ms,
                             uint32_t dec_ms)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->step_start == NULL) {
        return MOTION_ERR_DEVICE;
    }

    motion_device_note_start(MOTION_DEVICE_STEP, id);
    return call_result(ops->step_start(id, dir, speed, unit, pulse,
                                       time_ms, acc_ms, dec_ms));
}

/* 停止步进运动并清除占用标记。 */
int motion_device_step_stop(uint16_t id, uint32_t dec_ms)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->step_stop == NULL) {
        return MOTION_ERR_DEVICE;
    }

    motion_device_note_stop(MOTION_DEVICE_STEP, id);
    return call_result(ops->step_stop(id, dec_ms));
}

/* 设置步进速度。 */
int motion_device_step_set_speed(uint16_t id, double speed,
                                 motion_unit_e unit)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->step_set_speed == NULL) {
        return MOTION_ERR_DEVICE;
    }
    return call_result(ops->step_set_speed(id, speed, unit));
}

/* 设置步进方向。 */
int motion_device_step_set_dir(uint16_t id, uint8_t dir)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->step_set_dir == NULL) {
        return MOTION_ERR_DEVICE;
    }
    return call_result(ops->step_set_dir(id, dir));
}

/* 使能/失能步进驱动器。 */
int motion_device_step_enable(uint16_t id, uint8_t enable)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->step_enable == NULL) {
        return MOTION_ERR_DEVICE;
    }
    return call_result(ops->step_enable(id, enable));
}

/* 步进回零并标记设备占用。 */
int motion_device_step_home(uint16_t id, double speed, motion_unit_e unit,
                            uint8_t dir, uint32_t timeout_ms)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->step_home == NULL) {
        return MOTION_ERR_DEVICE;
    }

    motion_device_note_start(MOTION_DEVICE_STEP, id);
    return call_result(ops->step_home(id, speed, unit, dir, timeout_ms));
}

/* 查询步进是否忙。
 * 参数：id 设备号；busy 输出是否忙，不可为 NULL。
 * 返回：0 成功；MOTION_ERR_DEVICE 前置条件不满足；
 *       MOTION_ERR_DEVICE_EXEC 回调返回负值。
 * 说明：回调返回值 >0 表示忙。 */
int motion_device_step_is_busy(uint16_t id, bool *busy)
{
    const motion_device_ops_t *ops = get_ops();
    int result;

    if (ops == NULL || ops->step_is_busy == NULL || busy == NULL) {
        return MOTION_ERR_DEVICE;
    }

    result = ops->step_is_busy(id);
    if (result < 0) {
        return MOTION_ERR_DEVICE_EXEC;
    }

    *busy = (result != 0);
    return 0;
}

/* 读取步进当前位置。 */
int motion_device_step_position_read(uint16_t id, int32_t *position)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->step_position_read == NULL ||
        position == NULL) {
        return MOTION_ERR_DEVICE;
    }
    return call_result(ops->step_position_read(id, position));
}

/* 设置步进当前位置。 */
int motion_device_step_position_set(uint16_t id, int32_t position)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->step_position_set == NULL) {
        return MOTION_ERR_DEVICE;
    }
    return call_result(ops->step_position_set(id, position));
}

/* 启动 PWM 并标记设备占用。 */
int motion_device_pwm_start(uint16_t id, uint32_t freq_hz,
                            uint16_t duty_percent, uint32_t accel_ms)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->pwm_start == NULL) {
        return MOTION_ERR_DEVICE;
    }

    motion_device_note_start(MOTION_DEVICE_PWM, id);
    return call_result(ops->pwm_start(id, freq_hz, duty_percent, accel_ms));
}

/* 设置 PWM 占空比。 */
int motion_device_pwm_set_duty(uint16_t id, uint16_t duty_percent)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->pwm_set_duty == NULL) {
        return MOTION_ERR_DEVICE;
    }
    return call_result(ops->pwm_set_duty(id, duty_percent));
}

/* 设置 PWM 频率。 */
int motion_device_pwm_set_freq(uint16_t id, uint32_t freq_hz)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->pwm_set_freq == NULL) {
        return MOTION_ERR_DEVICE;
    }
    return call_result(ops->pwm_set_freq(id, freq_hz));
}

/* 停止 PWM 并清除设备占用标记。 */
int motion_device_pwm_stop(uint16_t id, uint32_t decel_ms)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->pwm_stop == NULL) {
        return MOTION_ERR_DEVICE;
    }

    motion_device_note_stop(MOTION_DEVICE_PWM, id);
    return call_result(ops->pwm_stop(id, decel_ms));
}

/* 查询 PWM 是否忙；回调返回值 >0 表示仍在输出。 */
int motion_device_pwm_is_busy(uint16_t id, bool *busy)
{
    const motion_device_ops_t *ops = get_ops();
    int result;

    if (ops == NULL || ops->pwm_is_busy == NULL || busy == NULL) {
        return MOTION_ERR_DEVICE;
    }

    result = ops->pwm_is_busy(id);
    if (result < 0) {
        return MOTION_ERR_DEVICE_EXEC;
    }

    *busy = (result != 0);
    return 0;
}

/* 写 GPIO 电平。 */
int motion_device_gpio_write(uint16_t id, uint8_t value)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->gpio_write == NULL) {
        return MOTION_ERR_DEVICE;
    }
    return call_result(ops->gpio_write(id, value));
}

/* 读 GPIO 电平；value 不可为 NULL。 */
int motion_device_gpio_read(uint16_t id, uint8_t *value)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->gpio_read == NULL || value == NULL) {
        return MOTION_ERR_DEVICE;
    }
    return call_result(ops->gpio_read(id, value));
}

/* 写继电器状态。 */
int motion_device_relay_write(uint16_t id, uint8_t value)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->relay_write == NULL) {
        return MOTION_ERR_DEVICE;
    }
    return call_result(ops->relay_write(id, value));
}

/* 读继电器状态；value 不可为 NULL。 */
int motion_device_relay_read(uint16_t id, uint8_t *value)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->relay_read == NULL || value == NULL) {
        return MOTION_ERR_DEVICE;
    }
    return call_result(ops->relay_read(id, value));
}

/* 读 ADC 采样值；value 不可为 NULL。 */
int motion_device_adc_read(uint16_t id, int32_t *value)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->adc_read == NULL || value == NULL) {
        return MOTION_ERR_DEVICE;
    }
    return call_result(ops->adc_read(id, value));
}

/* 读传感器值；value 不可为 NULL。 */
int motion_device_sensor_read(uint16_t id, int32_t *value)
{
    const motion_device_ops_t *ops = get_ops();

    if (ops == NULL || ops->sensor_read == NULL || value == NULL) {
        return MOTION_ERR_DEVICE;
    }
    return call_result(ops->sensor_read(id, value));
}

/* 标记设备开始占用。
 * 参数：family 设备类别；id 设备号。
 * 说明：id >= MOTION_MAX_DEVICES 时直接忽略，避免位图越界。 */
void motion_device_note_start(motion_device_family_e family, uint16_t id)
{
    if (id >= MOTION_MAX_DEVICES) {
        return;
    }
    if (family == MOTION_DEVICE_STEP) {
        if (s_step_active != NULL) {
            s_step_active[id] = true;
        }
    } else {
        if (s_pwm_active != NULL) {
            s_pwm_active[id] = true;
        }
    }
}

/* 标记设备释放；id 越界时忽略。 */
void motion_device_note_stop(motion_device_family_e family, uint16_t id)
{
    if (id >= MOTION_MAX_DEVICES) {
        return;
    }
    if (family == MOTION_DEVICE_STEP) {
        if (s_step_active != NULL) {
            s_step_active[id] = false;
        }
    } else {
        if (s_pwm_active != NULL) {
            s_pwm_active[id] = false;
        }
    }
}

/* 查询是否仍有被标记的设备在忙。
 * 参数：busy 输出标志，不可为 NULL。
 * 返回：0 成功；MOTION_ERR_PARAMETER busy 为 NULL；
 *       查询过程中若某个设备返回错误，则立即返回该错误码。
 * 说明：只检查位图中被标记为占用的设备，未启动过的设备不会调用回调。 */
int motion_device_any_busy(bool *busy)
{
    uint16_t i;

    if (busy == NULL) {
        return MOTION_ERR_PARAMETER;
    }

    *busy = false;

    if (s_step_active == NULL || s_pwm_active == NULL) {
        return 0; /* 尚未注册/分配，必然没有设备被标记 */
    }

    for (i = 0U; i < MOTION_MAX_DEVICES; i++) {
        bool value = false;

        if (s_step_active[i]) {
            int rc = motion_device_step_is_busy(i, &value);

            if (rc != 0) {
                return rc;
            }
            if (value) {
                *busy = true;
            }
        }

        if (s_pwm_active[i]) {
            int rc = motion_device_pwm_is_busy(i, &value);

            if (rc != 0) {
                return rc;
            }
            if (value) {
                *busy = true;
            }
        }
    }

    return 0;
}
