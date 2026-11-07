#ifndef AGS02MA_REGISTER_H
#define AGS02MA_REGISTER_H

/*
 * AGS02MA TVOC 传感器——寄存器式 I2C（8 位寄存器地址），默认地址 0x1A。
 *
 * 数据格式：先写 1 字节寄存器地址，再读 N 字节数据 + 1 字节 CRC。
 * CRC：CRC-8，poly 0x31（反射），init 0xFF，refin/refout = true（Maxim 风格）。
 */

#define AGS02MA_DEFAULT_ADDRESS      0x1AU

/* 寄存器 */
#define AGS02MA_REG_TVOC             0x00U  /* 读：4 字节 TVOC(ppb) + CRC */
#define AGS02MA_REG_CALIB_ZERO       0x01U  /* 写：零点校准 */
#define AGS02MA_REG_CALIB_TARGET     0x02U  /* 写：目标浓度校准 */
#define AGS02MA_REG_VERSION          0x11U  /* 读：2 字节版本号 + CRC */

/* CRC 参数 */
#define AGS02MA_CRC_POLY_REFLECTED   0x8CU  /* 0x31 反射后的值 */
#define AGS02MA_CRC_INIT             0xFFU

/* 读取周期：器件内部更新周期约 1.5s，更快读取只会拿到同一份数据 */
#define AGS02MA_UPDATE_MS            1500U

/* TVOC 合理上限（ppb），超出视为数据异常 */
#define AGS02MA_TVOC_MAX_PPB         1000000UL

#endif /* AGS02MA_REGISTER_H */
