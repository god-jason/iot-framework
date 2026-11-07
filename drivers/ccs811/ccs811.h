#ifndef CCS811_H
#define CCS811_H

/*
 * CCS811 eCO2 / TVOC 气体传感器驱动（I2C，默认地址 0x5A）。
 *
 * 实现等级：完整（基础测量流程）。init 做软复位、HW_ID(0x81) 校验、
 * APP_VALID / APP_START 处理与测量模式配置；read 轮询 DATA_READY 后
 * 读取 8 字节算法结果。
 *
 * 说明：eCO2/TVOC 为传感器内部算法输出，精度依赖 48h 以上连续运行与
 * 正确的温湿度补偿（ccs811_set_environment）；基线(0x11)掉电不保持。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    uint32_t ready_timeout_ms;
    bool initialized;
} ccs811_handle_t;

typedef struct {
    uint16_t co2_ppm;    /* eCO2，ppm（400~8192 有效范围） */
    uint16_t tvoc_ppb;   /* TVOC，ppb */
    uint8_t error_id;    /* 结果块内的错误标志，0 = 无错误 */
} ccs811_data_t;

/** @param address 7 位地址，传 0 取 0x5A */
int ccs811_init(ccs811_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

int ccs811_deinit(ccs811_handle_t *handle);

/** 轮询数据就绪后读取结果；超时返回 PORT_TIMEOUT */
int ccs811_read(ccs811_handle_t *handle, ccs811_data_t *data);

/** 温湿度补偿（ENV_DATA 0x05），建议每次测量前用环境值更新 */
int ccs811_set_environment(ccs811_handle_t *handle, float humidity_rh, float temperature_c);

/** 读错误寄存器 ERROR_ID(0xE0) */
int ccs811_read_error(ccs811_handle_t *handle, uint8_t *error_id);

#ifdef __cplusplus
}
#endif

#endif /* CCS811_H */
