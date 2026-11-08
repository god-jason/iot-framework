#ifndef PMS5003_H
#define PMS5003_H

/*
 * PMS5003 颗粒物传感器驱动（UART 主动上报，帧解析）。
 *
 * 单位：pm*_atm / pm*_cf1 -> µg/m³；nc_* -> 每 0.1L 粒子数。
 *
 * 用法：UART 接收到的字节流直接喂给 pms5003_parse()，每次传入至少 32 字节
 * （可含多帧，只解析第一帧）。驱动不做流式缓冲，缓冲区由调用方提供。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool initialized;
} pms5003_handle_t;

typedef struct {
    uint16_t pm1_atm;     /* PM1.0 大气环境 µg/m³ */
    uint16_t pm25_atm;    /* PM2.5 大气环境 µg/m³ */
    uint16_t pm10_atm;    /* PM10  大气环境 µg/m³ */
    uint16_t pm1_cf1;     /* PM1.0 标准颗粒物 µg/m³ */
    uint16_t pm25_cf1;    /* PM2.5 标准颗粒物 µg/m³ */
    uint16_t pm10_cf1;    /* PM10  标准颗粒物 µg/m³ */
    uint16_t nc_03;       /* ≥0.3µm 粒子数 /0.1L */
    uint16_t nc_05;
    uint16_t nc_10;
    uint16_t nc_25;
    uint16_t nc_50;
    uint16_t nc_100;
    uint16_t version;
} pms5003_data_t;

/** 初始化驱动（UART 由上层自行配置，本驱动不访问硬件） */
int pms5003_init(pms5003_handle_t *handle);

int pms5003_deinit(pms5003_handle_t *handle);

/**
 * @brief  解析一帧（32 字节）
 * @param  buffer 帧数据
 * @param  length 可用字节数，需 ≥ 32
 * @param  data   输出结果，不可为 NULL
 * @retval HAL_INVALID_PARAM 参数非法/长度不足
 * @retval HAL_CRC_ERROR     帧头或校验和错误
 */
int pms5003_parse(pms5003_handle_t *handle, const uint8_t *buffer, size_t length,
                  pms5003_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* PMS5003_H */
