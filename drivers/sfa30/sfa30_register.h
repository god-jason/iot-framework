#ifndef SFA30_REGISTER_H
#define SFA30_REGISTER_H

/*
 * SFA30 甲醛（HCHO）传感器——命令式 I2C，默认地址 0x5D。
 * 命令字与每个 16 位数据字后都跟随 1 字节 CRC8。
 */

#define SFA30_DEFAULT_ADDRESS        0x5DU

/* 命令 */
#define SFA30_CMD_START_CONTINUOUS   0x0006U  /* 参数：2 字节 + CRC */
#define SFA30_CMD_STOP_CONTINUOUS    0x0104U
#define SFA30_CMD_READ_VALUES        0x0327U  /* 3 个数据字：HCHO / RH / T */
#define SFA30_CMD_READ_SERIAL        0xD033U  /* 48 字节 */
#define SFA30_CMD_DEVICE_RESET       0xD304U

/* 时序（ms） */
#define SFA30_RESET_DELAY_MS         100U
#define SFA30_START_DELAY_MS         50U
#define SFA30_READ_DELAY_MS          2U

/* CRC8：poly 0x31, init 0xFF, MSB first */
#define SFA30_CRC_POLY               0x31U
#define SFA30_CRC_INIT               0xFFU

/* 换算：HCHO[ppb] = ticks / 5；RH[%] = 100*ticks/65536；
 * T[℃] = 200*ticks/65536 - 45（温度缩放需按 datasheet 核对） */
#define SFA30_HCHO_DIVISOR           5.0f
#define SFA30_RH_SCALE               100.0f
#define SFA30_T_SCALE                200.0f
#define SFA30_T_OFFSET               (-45.0f)
#define SFA30_RAW_MAX                65536.0f

#endif /* SFA30_REGISTER_H */
