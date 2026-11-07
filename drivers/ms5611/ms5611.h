#ifndef MS5611_H
#define MS5611_H

/*
 * MS5611-01BA 气压 / 温度传感器驱动（I2C 或 SPI）。
 *
 * 参考 libdriver 风格：
 *   ms5611_init / ms5611_init_spi / ms5611_deinit / ms5611_read
 *
 * MS5611 没有 chip id 寄存器，初始化时通过读取 8 字 PROM 并校验
 * word0 中的 CRC4 来确认器件与校准数据有效；补偿采用 datasheet 的
 * 二阶温度补偿算法（含低温二次修正），输出 ℃ 与 Pa。支持多实例。
 */

#include "common/driver_io.h"
#include "ms5611_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    driver_spi_t spi;
    bool use_spi;
    uint16_t prom[MS5611_PROM_WORDS];
    uint8_t osr;
    bool initialized;
} ms5611_handle_t;

typedef struct {
    float temperature;  /* ℃ */
    float pressure;     /* Pa */
} ms5611_data_t;

/** 初始化（I2C）：复位、读 PROM、校验 CRC4 */
int ms5611_init(ms5611_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** 初始化（SPI，cs 为片选引脚） */
int ms5611_init_spi(ms5611_handle_t *handle, port_spi_handle_t *spi, port_pin_t cs);

/** 反初始化（复位器件，不关闭总线） */
int ms5611_deinit(ms5611_handle_t *handle);

/** 读取一次温度 ℃ 与气压 Pa（内部完成 D1/D2 转换与二阶补偿） */
int ms5611_read(ms5611_handle_t *handle, ms5611_data_t *data);

/** 读取原始 D1（压力）/ D2（温度），任一指针可为 NULL */
int ms5611_read_raw(ms5611_handle_t *handle, int32_t *d1, int32_t *d2);

/** 拷贝初始化时读到的 8 字 PROM（prom[0] 含 CRC 高 4 位） */
int ms5611_get_prom(const ms5611_handle_t *handle, uint16_t *prom, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* MS5611_H */
