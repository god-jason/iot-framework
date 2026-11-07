#ifndef SGP30_REGISTER_H
#define SGP30_REGISTER_H

/*
 * SGP30 空气质量传感器（TVOC / eCO2）——命令式 I2C，无寄存器地址。
 * 每个命令为 16 位，命令字与每个 16 位数据字后都跟随 1 字节 CRC8。
 */

#define SGP30_DEFAULT_ADDRESS        0x58U

/* 命令 */
#define SGP30_CMD_IAQ_INIT           0x2003U  /* 初始化 IAQ 算法 */
#define SGP30_CMD_MEASURE_IAQ        0x2008U  /* 启动一次 IAQ 测量 */
#define SGP30_CMD_GET_BASELINE       0x2015U  /* 读基线 */
#define SGP30_CMD_SET_BASELINE       0x201EU  /* 写基线（2 个数据字） */
#define SGP30_CMD_SET_HUMIDITY       0x2061U  /* 湿度补偿（1 个数据字） */
#define SGP30_CMD_MEASURE_TEST       0x2032U  /* 自检，返回 0xD400 为正常 */
#define SGP30_CMD_GET_FEATURE_SET    0x202FU  /* 读特性集（版本号） */
#define SGP30_CMD_GET_SERIAL_ID      0x3682U  /* 读序列号（3 个数据字） */
#define SGP30_CMD_SOFT_RESET         0x0006U

/* 时序（ms） */
#define SGP30_RESET_DELAY_MS         10U
#define SGP30_IAQ_INIT_DELAY_MS      10U
#define SGP30_MEASURE_DELAY_MS       12U
#define SGP30_SELFTEST_DELAY_MS      220U

/* CRC8：poly 0x31, init 0xFF, MSB first */
#define SGP30_CRC_POLY               0x31U
#define SGP30_CRC_INIT               0xFFU

/* 特性集版本（ID 校验）：0x0020 / 0x0022 视固件版本而定 */
#define SGP30_FEATURE_SET_A          0x0020U
#define SGP30_FEATURE_SET_B          0x0022U

#endif /* SGP30_REGISTER_H */
