#ifndef AGS10_REGISTER_H
#define AGS10_REGISTER_H

/*
 * AGS10 TVOC 传感器——寄存器式 I2C（8 位寄存器地址），默认地址 0x1A。
 *
 * 数据格式：写 1 字节寄存器地址后读 N 字节数据 + 1 字节 CRC。
 * CRC：CRC-8，poly 0x31（反射），init 0xFF，refin/refout = true。
 */

#define AGS10_DEFAULT_ADDRESS        0x1AU

/* 寄存器 */
#define AGS10_REG_VOC                0x00U  /* 读：4 字节电阻值 + CRC */
#define AGS10_REG_CALIB              0x01U  /* 写：校准（4 字节 + CRC） */
#define AGS10_REG_RESET              0x06U  /* 写：0x01 软复位 */
#define AGS10_REG_VERSION            0x11U  /* 读：2 字节版本号 + CRC */

/* CRC 参数 */
#define AGS10_CRC_POLY_REFLECTED     0x8CU  /* 0x31 反射 */
#define AGS10_CRC_INIT               0xFFU

/* 读取周期：器件内部更新周期约 1s */
#define AGS10_UPDATE_MS              1000U

#endif /* AGS10_REGISTER_H */
