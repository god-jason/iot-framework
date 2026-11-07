#ifndef CS100_H
#define CS100_H

/*
 * CS100 CO2 传感器驱动（UART 帧解析，单位 ppm）。
 *
 * 用法：UART 收到的字节流喂给 cs100_parse()，缓冲区由调用方提供。
 *
 * 已知限制：CS100 的 UART 帧布局需按实际 datasheet 核对（见
 * cs100_register.h）；此外 I2C 模式未实现——CS100 的 I2C 从机地址与寄存器
 * 映射在不同资料中不一致，未取得 datasheet 前不做猜测实现。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool initialized;
} cs100_handle_t;

typedef struct {
    uint16_t co2_ppm;
} cs100_data_t;

/** 初始化驱动（UART 由上层配置，本驱动不访问硬件） */
int cs100_init(cs100_handle_t *handle);

int cs100_deinit(cs100_handle_t *handle);

/**
 * @brief  解析一帧
 * @param  buffer 帧数据
 * @param  length 可用字节数，需 ≥ CS100_FRAME_LEN
 * @param  data   输出结果
 * @retval PORT_CRC_ERROR 帧头或校验错误
 */
int cs100_parse(cs100_handle_t *handle, const uint8_t *buffer, size_t length,
                cs100_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* CS100_H */
