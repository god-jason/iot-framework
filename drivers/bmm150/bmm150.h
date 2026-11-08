#ifndef BMM150_H
#define BMM150_H

/*
 * BMM150 三轴磁力计驱动（I2C 0x10/0x11 或 SPI）。
 *
 * 参考 libdriver 风格：
 *   bmm150_init / bmm150_deinit / bmm150_read / bmm150_read_raw
 *
 * 设计要点：
 *   - init 校验 CHIP_ID(0x00，期望 0x32)，随后软复位并再次读 ID 确认复位成功；
 *   - 每次 read 触发一次 forced 模式测量，轮询 INT_STATUS.DRDY 后读数据；
 *   - 原始 13/15 位数据按标称灵敏度换算为 µT，并可用 set_calibration
 *     扣除硬铁偏置；
 *   - 完整软铁/椭球标定不在驱动内实现（见 bmm150_read 注释）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x;
    float y;
    float z;
} bmm150_vec3_t;

typedef struct {
    bmm150_vec3_t mag_ut;       /* 磁场，µT（已扣除校准偏置） */
    float heading_deg;          /* atan2(y, x) 转 0~360°，未做倾斜补偿 */
    uint16_t rhall;             /* 霍尔电阻辅助值，可用于温度/补偿估算 */
} bmm150_data_t;

typedef struct {
    driver_i2c_t i2c;
    driver_spi_t spi;
    bool use_spi;
    float offset_ut[3];         /* 软件硬铁偏置，默认 0 */
    bool initialized;
} bmm150_handle_t;

/**
 * @brief  初始化 BMM150
 * @param  handle   驱动句柄
 * @param  i2c      I2C 主机句柄，SPI 模式传 NULL
 * @param  spi      SPI 主机句柄，I2C 模式传 NULL
 * @param  cs       片选引脚，总线自带片选时传 0
 * @param  address  7 位从机地址，0 表示默认 0x10（SPI 模式忽略）
 * @return 0 成功，负值为 hal_status_t
 */
int bmm150_init(bmm150_handle_t *handle, hal_i2c_t *i2c,
                hal_spi_t *spi, hal_pin_t cs, uint16_t address);

/** @brief 反初始化：进入挂起模式 */
int bmm150_deinit(bmm150_handle_t *handle);

/** @brief 触发一次测量并输出 µT 与航向角 */
int bmm150_read(bmm150_handle_t *handle, bmm150_data_t *data);

/** @brief 触发一次测量并输出原始计数 [x, y, z]（13/15 位有符号） */
int bmm150_read_raw(bmm150_handle_t *handle, int16_t xyz[3]);

/** @brief 读取 INT_STATUS（bit0 数据就绪） */
int bmm150_get_status(bmm150_handle_t *handle, uint8_t *int_status);

/**
 * @brief 设置硬铁偏置（µT），从测量值中扣除
 * @note  偏置需由调用方通过旋转采样（椭球拟合/最小最大值法）得到
 */
int bmm150_set_calibration(bmm150_handle_t *handle, float offset_x, float offset_y, float offset_z);

#ifdef __cplusplus
}
#endif

#endif /* BMM150_H */
