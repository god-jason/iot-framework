#ifndef MLX90640_REGISTER_H
#define MLX90640_REGISTER_H

/*
 * MLX90640 32x24 红外热像阵列寄存器与常量（Melexis，I2C 7 位地址 0x33）。
 *
 * 该器件使用 16 位寄存器地址（先高字节后低字节），因此驱动必须走
 * driver_i2c_*_reg16()，不能使用 8 位寄存器接口。
 */

#define MLX90640_DEFAULT_ADDRESS    0x33U

/* 控制/状态寄存器 */
#define MLX90640_REG_CONTROL1       0x8000U
#define MLX90640_REG_CONTROL2       0x800DU
#define MLX90640_REG_STATUS         0x800FU

/* 测量 RAM：0x0400~0x073F 共 832 字（768 像素 + 辅助数据） */
#define MLX90640_REG_RAM_START      0x0400U
#define MLX90640_REG_RAM_END        0x073FU

/* 校准数据 EEPROM 起始地址（本驱动不解析，见 mlx90640.h 的限制说明） */
#define MLX90640_REG_EEPROM_START   0x2400U

/* CONTROL2[2:0]：刷新率编码（0x00=0.5Hz ... 0x07=64Hz）。
 * CONTROL2[5:4] 为 ADC 分辨率，改动会改变原始码的刻度，因此只做位域读改写。 */
#define MLX90640_CONTROL2_RATE_MASK 0x0007U
#define MLX90640_REFRESH_RATE_8HZ   0x0004U

/* STATUS(0x800F) 位定义（Melexis 数据手册）：
 *   bit0  新帧数据就绪（本驱动按固定帧周期轮询，不使用该位）
 *   bit3  当前 RAM 中子页号（Chess 模式 0/1）
 *   bit4  错误标志组（置位表示 RAM 内容异常）
 * 写 1 到 bit3 可清除子页标志，允许缓存下一帧。 */
#define MLX90640_STATUS_DATA_READY  0x0001U
#define MLX90640_STATUS_SUBPAGE     0x0008U
#define MLX90640_STATUS_ERROR_MASK  0x0010U
#define MLX90640_STATUS_SUBPAGE_CLR 0x0008U

/* 阵列尺寸 */
#define MLX90640_COLUMNS            32U
#define MLX90640_ROWS               24U
#define MLX90640_PIXELS             (MLX90640_COLUMNS * MLX90640_ROWS)

/* RAM 必须按 16 字（32 字节）分块读取 */
#define MLX90640_CHUNK_WORDS        16U

/* 时序：8Hz 刷新率 => 125ms 一帧 */
#define MLX90640_FRAME_PERIOD_8HZ_MS 125U
#define MLX90640_WARMUP_MS          300U

/*
 * 原始码 -> 温度 的近似系数。
 * 像素字为 16 位 ADC 结果，低 2 位为标志位；温度换算严格依赖 EEPROM 中
 * 832 字的逐像素 offset/灵敏度与 Ta 补偿（数据手册第 11 章），
 * 本驱动不做校准，仅沿用旧实现的线性假设：0.25 K/LSB。
 * 该系数未经标定，绝对精度不保证，仅适合相对热场/热点定位。
 */
#define MLX90640_RAW_SHIFT          2U
#define MLX90640_K_PER_LSB          0.25f
#define MLX90640_KELVIN_OFFSET      273.15f

#endif /* MLX90640_REGISTER_H */
