#ifndef NRF905_REGISTER_H
#define NRF905_REGISTER_H

/*
 * nRF905 433/868/915MHz 收发器寄存器与 SPI 命令定义。
 *
 * 注意：nRF905 没有器件 ID 寄存器，驱动以“写入配置后回读比对”作为探测手段。
 * 配置区前 10 字节为 RF-CONFIG 区，其余为收发地址/payload 区；其中
 * AFW、输出功率等位域的确切位置需按 datasheet 复核，本驱动只处理
 * 通道号（CH_NO）与频段选择（HFREQ_PLL）两个字段。
 */

/* ---------------- SPI 命令 ---------------- */
#define NRF905_CMD_W_CONFIG      0x00U
#define NRF905_CMD_R_CONFIG      0x10U
#define NRF905_CMD_W_TX_PAYLOAD  0x20U
#define NRF905_CMD_R_TX_PAYLOAD  0x21U
#define NRF905_CMD_W_TX_ADDRESS  0x22U
#define NRF905_CMD_R_TX_ADDRESS  0x23U
#define NRF905_CMD_R_RX_PAYLOAD  0x24U

/* ---------------- 配置区 ---------------- */
#define NRF905_CFG_SIZE          10U
#define NRF905_CFG_CH_NO         0U    /* CH_NO[7:0] */
#define NRF905_CFG_RF_CONFIG     1U    /* bit0=CH_NO[8], bit1=HFREQ_PLL */

#define NRF905_RF_CONFIG_CH8     0x01U
#define NRF905_RF_CONFIG_HFREQ   0x02U

/* ---------------- 频段与通道 ---------------- */
#define NRF905_FREQ_LOW_MIN_KHZ   422400U
#define NRF905_FREQ_LOW_MAX_KHZ   473500U
#define NRF905_FREQ_HIGH_MIN_KHZ  862000U
#define NRF905_FREQ_HIGH_MAX_KHZ  932000U
#define NRF905_FREQ_STEP_KHZ      100000U

/* ---------------- 数据接口 ---------------- */
#define NRF905_PAYLOAD_SIZE       32U    /* ShockBurst 固定 32 字节 */
#define NRF905_ADDRESS_SIZE       4U
#define NRF905_STATUS_DR          0x20U
#define NRF905_STATUS_AM          0x40U

#define NRF905_TX_SETTLE_US       650U   /* TX 建立时间 */
#define NRF905_RX_SETTLE_US       650U

#endif /* NRF905_REGISTER_H */
