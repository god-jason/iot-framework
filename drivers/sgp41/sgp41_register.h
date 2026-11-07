#ifndef SGP41_REGISTER_H
#define SGP41_REGISTER_H

/*
 * SGP41 VOC / NOx 传感器——命令式 I2C，无寄存器地址。
 * 命令字与每个 16 位数据字后都跟随 1 字节 CRC8。
 */

#define SGP41_DEFAULT_ADDRESS        0x59U

/* 命令 */
#define SGP41_CMD_EXECUTE_CONDITIONING   0x2808U  /* 10s 调节，返回 VOC/NOx 原始值 */
#define SGP41_CMD_MEASURE_RAW            0x2610U  /* 原始信号（无温湿度补偿） */
#define SGP41_CMD_MEASURE_RAW_COMP        0x2612U  /* 原始信号（带 RH/T 补偿参数） */
#define SGP41_CMD_SELF_TEST              0x2802U  /* 自检，0xD400 为通过 */
#define SGP41_CMD_GET_SERIAL_ID          0x3682U  /* 3 个数据字 */
#define SGP41_CMD_SOFT_RESET             0x0006U

/* 时序（ms） */
#define SGP41_RESET_DELAY_MS         10U
#define SGP41_MEASURE_DELAY_MS       50U
#define SGP41_CONDITIONING_MS        10000U
#define SGP41_SELFTEST_DELAY_MS      320U

/* CRC8：poly 0x31, init 0xFF, MSB first */
#define SGP41_CRC_POLY               0x31U
#define SGP41_CRC_INIT               0xFFU

/* 自检通过值 */
#define SGP41_SELFTEST_PASS          0xD400U

#endif /* SGP41_REGISTER_H */
