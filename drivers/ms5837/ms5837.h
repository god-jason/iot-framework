#ifndef MS5837_H
#define MS5837_H

/*
 * MS5837-30BA 深度 / 压力 / 温度传感器驱动（I2C）。
 *
 * 参考 libdriver 风格：
 *   ms5837_init / ms5837_deinit / ms5837_read
 *
 * 初始化时读取 8 字 PROM 并校验 CRC4（该器件无 chip id 寄存器，
 * PROM 的 CRC4 是确认器件与校准数据有效的标准手段）；
 * 读取时完成 D1/D2 转换，按 datasheet 的二阶温度补偿算法换算 ℃ 与 Pa，
 * 并给出以淡水（ρ = 1000 kg/m^3）为基准的估算水深。支持多实例。
 */

#include "common/driver_io.h"
#include "ms5837_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    uint16_t prom[MS5837_PROM_WORDS];
    uint8_t osr;
    bool initialized;
} ms5837_handle_t;

typedef struct {
    float temperature;  /* ℃ */
    float pressure;     /* Pa（绝压） */
    float depth_m;      /* m，以 1013.25 mbar 为水面基准的估算值 */
} ms5837_data_t;

/** 初始化：复位、读 PROM、校验 CRC4 */
int ms5837_init(ms5837_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

/** 反初始化（不关闭总线） */
int ms5837_deinit(ms5837_handle_t *handle);

/** 读取一次温度 ℃ / 绝压 Pa / 估算水深 m */
int ms5837_read(ms5837_handle_t *handle, ms5837_data_t *data);

/** 读取原始 D1（压力）/ D2（温度），任一指针可为 NULL */
int ms5837_read_raw(ms5837_handle_t *handle, int32_t *d1, int32_t *d2);

/** 拷贝初始化时读到的 8 字 PROM（prom[0] 含 CRC 高 4 位） */
int ms5837_get_prom(const ms5837_handle_t *handle, uint16_t *prom, size_t count);

/** 由绝压换算水深（淡水，m），压力无效时返回 0 */
float ms5837_pressure_to_depth(float pressure_pa);

#ifdef __cplusplus
}
#endif

#endif /* MS5837_H */
