#ifndef PMS5003_REGISTER_H
#define PMS5003_REGISTER_H

/*
 * PMS5003 / PMS7003 / PMSx003 系列颗粒物传感器——UART 主动上报模式。
 *
 * 帧格式（共 32 字节，大端 16 位）：
 *   [0]=0x42 [1]=0x4D 帧头
 *   [2..3]  帧长度（不含帧头与校验，固定 0x001C = 28）
 *   [4..5]  PM1.0 大气环境 µg/m³
 *   [6..7]  PM2.5 大气环境
 *   [8..9]  PM10  大气环境
 *   [10..11] PM1.0 标准颗粒物（CF=1）
 *   [12..13] PM2.5 标准颗粒物
 *   [14..15] PM10  标准颗粒物
 *   [16..27] 各粒径粒子数 /0.1L（0.3/0.5/1.0/2.5/5.0/10 µm）
 *   [28..29] 版本号
 *   [30..31] 校验和 = 前 30 字节累加和
 */

#define PMS5003_HEAD0            0x42U
#define PMS5003_HEAD1            0x4DU
#define PMS5003_FRAME_LEN        32U
#define PMS5003_PAYLOAD_LEN      28U
#define PMS5003_CHECKSUM_INDEX   30U

/* 字段偏移 */
#define PMS5003_OFF_PM1_ATM      4U
#define PMS5003_OFF_PM25_ATM     6U
#define PMS5003_OFF_PM10_ATM     8U
#define PMS5003_OFF_PM1_CF1      10U
#define PMS5003_OFF_PM25_CF1     12U
#define PMS5003_OFF_PM10_CF1     14U
#define PMS5003_OFF_NC_03        16U
#define PMS5003_OFF_NC_05        18U
#define PMS5003_OFF_NC_10        20U
#define PMS5003_OFF_NC_25        22U
#define PMS5003_OFF_NC_50        24U
#define PMS5003_OFF_NC_100       26U
#define PMS5003_OFF_VERSION      28U

#endif /* PMS5003_REGISTER_H */
