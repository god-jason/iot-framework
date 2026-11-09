#ifndef MOTION_DEVICE_H
#define MOTION_DEVICE_H

/*
 * motion_device.h —— 设备操作抽象层（HAL 适配）接口。
 *
 * 职责：定义解释器需要的设备回调集合 motion_device_ops_t，并把上层的
 *       语义化调用（步进/ PWM / GPIO / 继电器 / ADC / 传感器）转成回调调用，
 *       同时统一返回值错误码、记录设备占用状态。
 *
 * 数据流：移植层通过 motion_device_register() 注册一份回调表 ->
 *         解释器调用 motion_device_xxx() -> 内部查表并调用对应回调
 *         -> 结果转换为 0 或 MOTION_ERR_* 返回。
 *
 * 边界约束：全局仅保存一份回调表（单例），重复注册会覆盖并清空占用记录；
 *           设备号合法范围 0 ~ MOTION_MAX_DEVICES-1，内部占用位图为定长数组；
 *           本模块不分配内存，非线程安全。
 */

#include "motion_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 设备回调表：由移植层实现，未实现的成员可置 NULL（调用时返回 DEVICE 错误）。
 * 所有回调返回值约定：0 成功，负值失败。 */
typedef struct {
    /* 启动步进运动。
     * id 设备号；dir 方向（0/1）；speed 速度；speed_unit 速度单位；
     * pulse 目标脉冲数；time_ms 总时长；acc_ms 加速时间；dec_ms 减速时间。
     * 返回 0 成功，负值失败。 */
    int (*step_start)(uint16_t id, uint8_t dir, double speed,
                      motion_unit_e speed_unit, int32_t pulse,
                      uint32_t time_ms, uint32_t acc_ms, uint32_t dec_ms);
    /* 停止步进运动，dec_ms 为减速时间（毫秒）。 */
    int (*step_stop)(uint16_t id, uint32_t dec_ms);
    /* 设置速度（仅在非运动状态生效或由实现决定）。 */
    int (*step_set_speed)(uint16_t id, double speed, motion_unit_e unit);
    /* 设置方向：dir 0/1。 */
    int (*step_set_dir)(uint16_t id, uint8_t dir);
    /* 使能/失能驱动器：enable 0 失能、非 0 使能。 */
    int (*step_enable)(uint16_t id, uint8_t enable);
    /* 回零：按 speed/unit 运动，dir 指定方向，timeout_ms 为 0 表示不限时。 */
    int (*step_home)(uint16_t id, double speed, motion_unit_e unit,
                     uint8_t dir, uint32_t timeout_ms);
    /* 查询是否运动中：返回 >0 忙，0 空闲，<0 出错。 */
    int (*step_is_busy)(uint16_t id);
    /* 读取当前位置（脉冲数）。 */
    int (*step_position_read)(uint16_t id, int32_t *position);
    /* 设置当前位置（脉冲数）。 */
    int (*step_position_set)(uint16_t id, int32_t position);

    /* 启动 PWM：频率 Hz、占空比 %、加速时间 ms。 */
    int (*pwm_start)(uint16_t id, uint32_t freq_hz, uint16_t duty_percent,
                     uint32_t accel_ms);
    /* 修改占空比（%）。 */
    int (*pwm_set_duty)(uint16_t id, uint16_t duty_percent);
    /* 修改频率（Hz）。 */
    int (*pwm_set_freq)(uint16_t id, uint32_t freq_hz);
    /* 停止 PWM，decel_ms 为减速时间（毫秒）。 */
    int (*pwm_stop)(uint16_t id, uint32_t decel_ms);
    /* 查询 PWM 是否输出中：返回 >0 忙，0 空闲，<0 出错。 */
    int (*pwm_is_busy)(uint16_t id);

    /* 写 GPIO：value 为电平（0/1）。 */
    int (*gpio_write)(uint16_t id, uint8_t value);
    /* 读 GPIO：value 输出电平（0/1）。 */
    int (*gpio_read)(uint16_t id, uint8_t *value);

    /* 写继电器：value 0 断开、非 0 吸合。 */
    int (*relay_write)(uint16_t id, uint8_t value);
    /* 读继电器状态。 */
    int (*relay_read)(uint16_t id, uint8_t *value);

    /* 读取 ADC 采样值。 */
    int (*adc_read)(uint16_t id, int32_t *value);
    /* 读取传感器值。 */
    int (*sensor_read)(uint16_t id, int32_t *value);
} motion_device_ops_t;

/* 设备类别：用于 WAIT ALL 时区分占用位图 */
typedef enum {
    MOTION_DEVICE_STEP = 0, /* 步进电机 */
    MOTION_DEVICE_PWM       /* PWM 输出 */
} motion_device_family_e;

/* 注册设备回调表（整体拷贝）。
 * 参数：ops 回调表，不可为 NULL。
 * 返回：0 成功；MOTION_ERR_PARAMETER ops 为 NULL。
 * 说明：会清空已有的设备占用记录。 */
int motion_device_register(const motion_device_ops_t *ops);
/* 获取当前已注册的回调表；未注册时返回 NULL。 */
const motion_device_ops_t *motion_device_get(void);
/* 复位：注销回调表并清空所有设备占用记录。 */
void motion_device_reset(void);

/* 设备执行封装：返回 MOTION_ERR_* */
/* 参数说明同回调原型；未注册或缺对应回调返回 MOTION_ERR_DEVICE，
 * 回调返回非 0 时统一折算为 MOTION_ERR_DEVICE_EXEC。 */
int motion_device_step_start(uint16_t id, uint8_t dir, double speed,
                             motion_unit_e unit, int32_t pulse,
                             uint32_t time_ms, uint32_t acc_ms,
                             uint32_t dec_ms);
/* 停止步进并清除该设备的占用标记。 */
int motion_device_step_stop(uint16_t id, uint32_t dec_ms);
/* 设置步进速度。 */
int motion_device_step_set_speed(uint16_t id, double speed,
                                 motion_unit_e unit);
/* 设置步进方向。 */
int motion_device_step_set_dir(uint16_t id, uint8_t dir);
/* 使能/失能步进驱动器。 */
int motion_device_step_enable(uint16_t id, uint8_t enable);
/* 步进回零并标记该设备占用。 */
int motion_device_step_home(uint16_t id, double speed, motion_unit_e unit,
                            uint8_t dir, uint32_t timeout_ms);
/* 查询步进是否忙：busy 输出 true 表示运动中。
 * 返回 0 成功；MOTION_ERR_DEVICE 未注册/回调缺失/busy 为 NULL；
 * MOTION_ERR_DEVICE_EXEC 回调返回负值。 */
int motion_device_step_is_busy(uint16_t id, bool *busy);
/* 读取步进位置。 */
int motion_device_step_position_read(uint16_t id, int32_t *position);
/* 设置步进位置。 */
int motion_device_step_position_set(uint16_t id, int32_t position);

/* 启动 PWM 并标记该设备占用。 */
int motion_device_pwm_start(uint16_t id, uint32_t freq_hz,
                            uint16_t duty_percent, uint32_t accel_ms);
/* 设置 PWM 占空比。 */
int motion_device_pwm_set_duty(uint16_t id, uint16_t duty_percent);
/* 设置 PWM 频率。 */
int motion_device_pwm_set_freq(uint16_t id, uint32_t freq_hz);
/* 停止 PWM 并清除该设备的占用标记。 */
int motion_device_pwm_stop(uint16_t id, uint32_t decel_ms);
/* 查询 PWM 是否忙：busy 输出 true 表示仍在输出。 */
int motion_device_pwm_is_busy(uint16_t id, bool *busy);

/* 写 GPIO。 */
int motion_device_gpio_write(uint16_t id, uint8_t value);
/* 读 GPIO；value 不可为 NULL。 */
int motion_device_gpio_read(uint16_t id, uint8_t *value);
/* 写继电器。 */
int motion_device_relay_write(uint16_t id, uint8_t value);
/* 读继电器；value 不可为 NULL。 */
int motion_device_relay_read(uint16_t id, uint8_t *value);
/* 读 ADC；value 不可为 NULL。 */
int motion_device_adc_read(uint16_t id, int32_t *value);
/* 读传感器；value 不可为 NULL。 */
int motion_device_sensor_read(uint16_t id, int32_t *value);

/* 已用设备占用记录，供 WAIT ALL 使用 */
/* 标记设备开始占用；id 越界（>= MOTION_MAX_DEVICES）时忽略。 */
void motion_device_note_start(motion_device_family_e family, uint16_t id);
/* 标记设备释放；id 越界时忽略。 */
void motion_device_note_stop(motion_device_family_e family, uint16_t id);
/* 遍历占用位图，查询是否有设备仍在忙。
 * 返回 0 成功（busy 已写入）；MOTION_ERR_PARAMETER busy 为 NULL；
 * 查询某个设备失败时原样返回其错误码。 */
int motion_device_any_busy(bool *busy);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_DEVICE_H */
