#ifndef SPS30_REGISTER_H
#define SPS30_REGISTER_H

/*
 * SPS30 颗粒物传感器——I2C 模式（默认地址 0x69），命令式接口。
 *
 * 本驱动只实现 I2C 模式；SPS30 也支持 UART，若使用 UART 需另做帧解析。
 * 命令字与每个 16 位数据字后都跟随 1 字节 CRC8；测量值为大端 float32。
 */

#define SPS30_DEFAULT_ADDRESS        0x69U

/* 命令 */
#define SPS30_CMD_START_MEASUREMENT  0x0010U  /* 参数：输出格式（float=0x0300） */
#define SPS30_CMD_STOP_MEASUREMENT   0x0104U
#define SPS30_CMD_DATA_READY         0x0202U
#define SPS30_CMD_READ_VALUES        0x0300U  /* 60 字节 = 10 个 float + CRC */
#define SPS30_CMD_READ_SERIAL        0xD033U  /* 48 字节 = 16 个字 + CRC */
#define SPS30_CMD_READ_VERSION       0xD100U
#define SPS30_CMD_START_FAN_CLEAN    0x5607U
#define SPS30_CMD_DEVICE_RESET       0xD304U

/* 测量输出格式参数：大端 0x03 + 0x00（float），后面跟 CRC */
#define SPS30_OUTPUT_FLOAT_HI        0x03U
#define SPS30_OUTPUT_FLOAT_LO        0x00U

/* 时序（ms） */
#define SPS30_RESET_DELAY_MS         100U
#define SPS30_START_DELAY_MS         20U
#define SPS30_READ_TIMEOUT_MS        1000U
#define SPS30_POLL_DELAY_MS          20U

/* CRC8：poly 0x31, init 0xFF, MSB first */
#define SPS30_CRC_POLY               0x31U
#define SPS30_CRC_INIT               0xFFU

/* 测量值个数（float）与数据字个数 */
#define SPS30_VALUE_COUNT            10U
#define SPS30_VALUE_WORDS            20U

#endif /* SPS30_REGISTER_H */
