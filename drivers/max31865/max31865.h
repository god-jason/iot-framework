#ifndef MAX31865_H
#define MAX31865_H

/*
 * MAX31865 RTD（PT100 / PT1000）温度转换器驱动（SPI）。
 *
 * 默认配置：自动转换模式、50Hz 陷波、VBIAS 常开、2/4 线制。
 * 温度换算使用 Callendar-Van Dusen 公式（IEC 60751），
 * 调用方需按板上参考电阻与 RTD 类型设置 rref / r0。
 *
 * 用法：
 *   max31865_handle_t rtd;
 *   max31865_init(&rtd, &spi1, PORT_PIN(1, 4));
 *   max31865_set_rref(&rtd, 430.0f);
 *   max31865_set_wire_mode(&rtd, 3);
 *   float t;
 *   max31865_read(&rtd, &t);
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_spi_t spi;
    float rref;      /* 参考电阻（Ω） */
    float r0;        /* RTD 0℃ 电阻（Ω） */
    uint8_t wire;    /* 接线方式：2 / 3 / 4 */
    bool initialized;
} max31865_handle_t;

/**
 * @brief  初始化并校验 SPI 通信
 * @param  cs  片选引脚，PORT_PIN(0,0) 表示由外部控制
 */
int max31865_init(max31865_handle_t *handle, port_spi_handle_t *spi, port_pin_t cs);

/** @brief  反初始化 */
int max31865_deinit(max31865_handle_t *handle);

/** @brief  设置接线方式（2 / 3 / 4 线），会重新写配置寄存器 */
int max31865_set_wire_mode(max31865_handle_t *handle, uint8_t wires);

/** @brief  设置参考电阻（Ω），须大于 0 */
int max31865_set_rref(max31865_handle_t *handle, float rref);

/** @brief  设置 RTD 在 0℃ 的标称电阻（PT100: 100Ω，PT1000: 1000Ω） */
int max31865_set_r0(max31865_handle_t *handle, float r0);

/** @brief  读取 15 位 RTD 原始码 */
int max31865_read_raw(max31865_handle_t *handle, uint16_t *raw);

/** @brief  读取 RTD 电阻值（Ω） */
int max31865_read_resistance(max31865_handle_t *handle, float *ohms);

/** @brief  读取温度（℃） */
int max31865_read(max31865_handle_t *handle, float *celsius);

/** @brief  读取故障状态寄存器 */
int max31865_get_fault(max31865_handle_t *handle, uint8_t *fault);

/** @brief  清除故障标志 */
int max31865_clear_fault(max31865_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* MAX31865_H */
