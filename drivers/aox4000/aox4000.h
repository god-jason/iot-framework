#ifndef AOX4000_H
#define AOX4000_H

/*
 * AOX4000 氧气传感器驱动（UART，帧解析）。
 *
 * 单位：o2_percent -> vol%（0~25）；raw -> 器件原始 16 位值。
 *
 * 用法：UART 收到的字节流喂给 aox4000_parse()，缓冲区由调用方提供。
 *
 * 已知限制：AOX4000 的 UART 帧布局在不同固件/资料中存在差异，本驱动采用
 * 9 字节定长帧 + 累加和校验的约定实现，字段偏移与量纲集中在
 * aox4000_register.h 中，**使用前必须按实际 datasheet 核对**，
 * 因此解析失败时返回 IOT_CRC_ERROR / IOT_INVALID_PARAM 而不做猜测性容错。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool initialized;
} aox4000_handle_t;

typedef struct {
    uint16_t raw;        /* 原始 16 位浓度值 */
    float o2_percent;    /* 氧气浓度 vol% */
    bool checksum_ok;    /* 校验通过标记（parse 成功时恒为 true） */
} aox4000_data_t;

/** 初始化驱动（UART 由上层配置，本驱动不访问硬件） */
int aox4000_init(aox4000_handle_t *handle);

int aox4000_deinit(aox4000_handle_t *handle);

/**
 * @brief  解析一帧
 * @param  buffer 帧数据
 * @param  length 可用字节数，需 ≥ AOX4000_FRAME_LEN
 * @param  data   输出结果
 * @retval IOT_CRC_ERROR 帧头或校验错误
 */
int aox4000_parse(aox4000_handle_t *handle, const uint8_t *buffer, size_t length,
                  aox4000_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* AOX4000_H */
