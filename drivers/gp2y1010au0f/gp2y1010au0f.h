#ifndef GP2Y1010AU0F_H
#define GP2Y1010AU0F_H

/*
 * GP2Y1010AU0F 粉尘传感器驱动（模拟量输入，输出 µg/m³）。
 *
 * 参考 libdriver 设计：
 *   gp2y1010au0f_init / _deinit / _read / _read_raw
 *
 * 旧实现没有 init，且完全忽略 IR LED 脉冲时序（LED 常灭时读数没有意义
 * 或严重偏低）。新实现：
 *   - 初始化统一配置 ADC 通道
 *   - LED 驱动引脚为**可选**：调用 gp2y1010au0f_set_led 后 read 会自动
 *     完成 "拉低点亮 -> 延时 280µs -> 采样 -> 熄灭 -> 等 9.68ms" 的时序；
 *     不设置则按常亮模块（已硬件拉低 LED）直接采样，兼容简化模块
 *
 * 硬件访问：直接调用 port_adc_* 与 driver_gpio_* / driver_delay_us，
 * 不包含 bus 目录头文件，不依赖任何 HAL。
 *
 * 用法：
 *   gp2y1010au0f_handle_t dust;
 *   gp2y1010au0f_init(&dust, &adc1, 7);
 *   gp2y1010au0f_set_led(&dust, PORT_PIN(0, 5));   （可选）
 *   float ug;
 *   gp2y1010au0f_read(&dust, &ug);
 *
 * 已知限制：单次采样噪声较大，建议上层做 8~16 次平均；基线随温湿度
 * 漂移，长期使用需定期在洁净空气中重标 Voff。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    port_adc_handle_t *adc;      /* 已初始化的 ADC 句柄 */
    uint8_t channel;             /* ADC 通道号 */
    float vref_mv;               /* ADC 参考电压 mV */
    uint32_t adc_max;            /* ADC 满量程码值 */
    float voff_mv;               /* 洁净空气基线 mV（标定参数） */
    port_pin_t led_pin;          /* IR LED 驱动引脚 */
    bool led_enabled;            /* true：read 时自动产生 LED 脉冲 */
    bool initialized;
} gp2y1010au0f_handle_t;

/**
 * @brief  初始化驱动并配置 ADC 通道
 * @param  adc      已初始化的 ADC 句柄（不可为 NULL）
 * @param  channel  ADC 通道号
 * @return 0 成功；PORT_INVALID_PARAM 参数非法；PORT_IO_ERROR 通道配置失败
 */
int gp2y1010au0f_init(gp2y1010au0f_handle_t *handle, port_adc_handle_t *adc, uint8_t channel);

/** @brief 反初始化（不关闭 ADC，不释放 LED 引脚，LED 保持熄灭） */
int gp2y1010au0f_deinit(gp2y1010au0f_handle_t *handle);

/**
 * @brief  读取粉尘浓度
 * @param  density_ug_m3  浓度，单位 µg/m³（已扣除基线，负值钳位 0）
 * @return 0 成功；PORT_NOT_READY 未初始化；PORT_IO_ERROR ADC 或 GPIO 失败
 */
int gp2y1010au0f_read(gp2y1010au0f_handle_t *handle, float *density_ug_m3);

/** @brief 读取 ADC 原始码值（不驱动 LED） */
int gp2y1010au0f_read_raw(gp2y1010au0f_handle_t *handle, uint32_t *raw);

/**
 * @brief  指定 IR LED 驱动引脚（低电平点亮），并初始化为推挽输出高（熄灭）
 * @return 0 成功；PORT_INVALID_PARAM 参数非法；PORT_IO_ERROR GPIO 初始化失败
 */
int gp2y1010au0f_set_led(gp2y1010au0f_handle_t *handle, port_pin_t pin);

/** @brief 写入洁净空气基线电压（mV） */
int gp2y1010au0f_set_voffset(gp2y1010au0f_handle_t *handle, float voff_mv);

/** @brief 覆盖 ADC 参考电压与满量程码值 */
int gp2y1010au0f_set_reference(gp2y1010au0f_handle_t *handle, float vref_mv, uint32_t adc_max);

#ifdef __cplusplus
}
#endif

#endif /* GP2Y1010AU0F_H */
