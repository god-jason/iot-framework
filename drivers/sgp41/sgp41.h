#ifndef SGP41_H
#define SGP41_H

/*
 * SGP41 VOC / NOx 传感器驱动（命令式 I2C，默认地址 0x59）。
 *
 * 单位说明：
 *   - sraw_voc / sraw_nox : 传感器原始信号（无量纲，0..65535），
 *     必须经 Sensirion VOC/NOx Gas Index Algorithm 才能得到 1..500 的
 *     VOC/NOx 指数；该算法为 Sensirion 专有库，本驱动不实现，
 *     也不编造近似的指数换算公式。
 *   - 上层若只需趋势监测，可直接使用原始值。
 *
 * 首次上电或长时间断电后应先执行 sgp41_execute_conditioning()（10s）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool initialized;
} sgp41_handle_t;

typedef struct {
    uint16_t sraw_voc;
    uint16_t sraw_nox;
} sgp41_data_t;

/** @param address 7 位地址，传 0 取 0x59 */
int sgp41_init(sgp41_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

int sgp41_deinit(sgp41_handle_t *handle);

/** 自检，返回 DRIVER_OK 表示通过（内部读取 0xD400 判定） */
int sgp41_self_test(sgp41_handle_t *handle);

/** 读 48 位序列号 */
int sgp41_get_serial(sgp41_handle_t *handle, uint64_t *serial);

/** 10s 调节流程，返回调节结束时的原始 VOC/NOx 值（可为 NULL） */
int sgp41_execute_conditioning(sgp41_handle_t *handle, sgp41_data_t *data);

/** 无温湿度补偿的原始测量 */
int sgp41_measure_raw(sgp41_handle_t *handle, sgp41_data_t *data);

/**
 * @brief  带温湿度补偿的原始测量
 * @param  rh_ticks 相对湿度 ticks = RH[%] * 65535 / 100
 * @param  t_ticks  温度 ticks = (T[℃] + 45) * 65535 / 175
 */
int sgp41_measure_raw_compensated(sgp41_handle_t *handle, uint16_t rh_ticks,
                                  uint16_t t_ticks, sgp41_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* SGP41_H */
