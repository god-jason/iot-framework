#ifndef MMA7660FC_H
#define MMA7660FC_H

/*
 * MMA7660FC 三轴加速度计驱动（I2C，7 位地址 0x4C）。
 *
 * 参考 libdriver 风格：
 *   mma7660fc_init / mma7660fc_deinit / mma7660fc_read / mma7660fc_read_raw
 *
 * 设计要点：
 *   - MMA7660FC 没有 WHO_AM_I 寄存器：init 先探测 ACK，再通过 PDET 寄存器
 *     写入/回读固定值完成在位与写通路自检，并回读 MODE 确认已进入工作状态；
 *   - 6 位补码输出，±1.5g 量程，21.33 counts/g，read 输出 g；
 *   - read_raw 输出 6 位有符号计数；
 *   - 采样率（1~120Hz）可配置；TILT 寄存器可读取姿态标志。
 *
 * 注意：超出 ±1.5g 的加速度会削顶，不适合冲击测量。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 枚举值与 SR 寄存器的 AMSR 编码一致 */
typedef enum {
    MMA7660FC_SR_120HZ = 0,
    MMA7660FC_SR_64HZ,
    MMA7660FC_SR_32HZ,
    MMA7660FC_SR_16HZ,
    MMA7660FC_SR_8HZ,
    MMA7660FC_SR_4HZ,
    MMA7660FC_SR_2HZ,
    MMA7660FC_SR_1HZ,
    MMA7660FC_SR_COUNT
} mma7660fc_sample_rate_t;

typedef struct {
    float x;
    float y;
    float z;
} mma7660fc_vec3_t;

typedef struct {
    mma7660fc_vec3_t accel_g;   /* 加速度，g（±1.5g） */
    bool alert;                 /* XOUT.ALERT：数据自上次读取后更新 */
    uint8_t tilt;               /* TILT 寄存器原始值，位定义见头文件宏 */
} mma7660fc_data_t;

typedef struct {
    driver_i2c_t i2c;
    mma7660fc_sample_rate_t sample_rate;
    bool initialized;
} mma7660fc_handle_t;

/**
 * @brief  初始化 MMA7660FC
 * @param  handle       驱动句柄
 * @param  i2c          已初始化的 I2C 主机句柄
 * @param  address      7 位从机地址，0 表示默认 0x4C
 * @param  sample_rate  自动唤醒采样率
 * @return 0 成功，负值为 iot_status_t
 */
int mma7660fc_init(mma7660fc_handle_t *handle, iot_i2c_t *i2c, uint16_t address,
                   mma7660fc_sample_rate_t sample_rate);

/** @brief 反初始化：进入待机模式 */
int mma7660fc_deinit(mma7660fc_handle_t *handle);

/** @brief 读取加速度（g）、ALERT 标志与 TILT 原始值 */
int mma7660fc_read(mma7660fc_handle_t *handle, mma7660fc_data_t *data);

/** @brief 读取原始计数 [x, y, z]（6 位补码，-32~31） */
int mma7660fc_read_raw(mma7660fc_handle_t *handle, int16_t xyz[3]);

/** @brief 读取 TILT 寄存器（姿态 / 敲击 / 摇晃标志） */
int mma7660fc_get_tilt(mma7660fc_handle_t *handle, uint8_t *tilt);

/** @brief 修改采样率 */
int mma7660fc_set_sample_rate(mma7660fc_handle_t *handle, mma7660fc_sample_rate_t sample_rate);

#ifdef __cplusplus
}
#endif

#endif /* MMA7660FC_H */
