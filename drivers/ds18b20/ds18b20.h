#ifndef DS18B20_H
#define DS18B20_H

/*
 * DS18B20 单总线温度传感器驱动。
 *
 * 1-Wire 时序由 GPIO 软件模拟，引脚须外接 4.7k 上拉电阻。
 * 参考 libdriver 风格：ds18b20_init / ds18b20_read / ds18b20_start。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hal_pin_t pin;
    bool initialized;
} ds18b20_handle_t;

/**
 * @brief  初始化（引脚配置为开漏输出 + 上拉）
 */
int ds18b20_init(ds18b20_handle_t *handle, hal_pin_t pin);
int ds18b20_deinit(ds18b20_handle_t *handle);

/**
 * @brief  总线复位并检测从机
 * @return 1 存在器件，0 无器件，负值为错误码
 */
int ds18b20_reset(ds18b20_handle_t *handle);

/** 启动一次温度转换（非阻塞，调用方自行等待 750ms） */
int ds18b20_start(ds18b20_handle_t *handle);

/** 读取暂存器（9 字节） */
int ds18b20_read_scratchpad(ds18b20_handle_t *handle, uint8_t scratchpad[9]);

/** 启动转换、等待并读取温度（℃），阻塞约 750ms */
int ds18b20_read(ds18b20_handle_t *handle, float *celsius);

#ifdef __cplusplus
}
#endif

#endif /* DS18B20_H */
