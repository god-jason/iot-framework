#ifndef NRF24L01_REGISTER_H
#define NRF24L01_REGISTER_H

/*
 * nRF24L01+ 寄存器与 SPI 命令定义。
 * 参考 Nordic nRF24L01+ Product Specification v1.0 第 8/9 章。
 */

/* ---------------- 寄存器地址 ---------------- */
#define NRF24L01_REG_CONFIG       0x00U
#define NRF24L01_REG_EN_AA        0x01U
#define NRF24L01_REG_EN_RXADDR    0x02U
#define NRF24L01_REG_SETUP_AW     0x03U
#define NRF24L01_REG_SETUP_RETR   0x04U
#define NRF24L01_REG_RF_CH        0x05U
#define NRF24L01_REG_RF_SETUP     0x06U
#define NRF24L01_REG_STATUS       0x07U
#define NRF24L01_REG_OBSERVE_TX   0x08U
#define NRF24L01_REG_RPD          0x09U
#define NRF24L01_REG_RX_ADDR_P0   0x0AU
#define NRF24L01_REG_RX_ADDR_P1   0x0BU
#define NRF24L01_REG_TX_ADDR      0x10U
#define NRF24L01_REG_RX_PW_P0     0x11U
#define NRF24L01_REG_FIFO_STATUS  0x17U
#define NRF24L01_REG_DYNPD        0x1CU
#define NRF24L01_REG_FEATURE      0x1DU

/* ---------------- SPI 命令 ---------------- */
#define NRF24L01_CMD_R_REGISTER        0x00U
#define NRF24L01_CMD_W_REGISTER        0x20U
#define NRF24L01_CMD_R_RX_PAYLOAD      0x61U
#define NRF24L01_CMD_W_TX_PAYLOAD      0xA0U
#define NRF24L01_CMD_FLUSH_TX          0xE1U
#define NRF24L01_CMD_FLUSH_RX          0xE2U
#define NRF24L01_CMD_REUSE_TX_PL       0xE3U
#define NRF24L01_CMD_ACTIVATE          0x50U
#define NRF24L01_CMD_R_RX_PL_WID       0x60U
#define NRF24L01_CMD_W_ACK_PAYLOAD     0xA8U
#define NRF24L01_CMD_W_TX_PAYLOAD_NOACK 0xB0U
#define NRF24L01_CMD_NOP               0xFFU

/* ---------------- CONFIG 位 ---------------- */
#define NRF24L01_CONFIG_MASK_RX_DR   0x40U
#define NRF24L01_CONFIG_MASK_TX_DS   0x20U
#define NRF24L01_CONFIG_MASK_MAX_RT  0x10U
#define NRF24L01_CONFIG_EN_CRC       0x08U
#define NRF24L01_CONFIG_CRCO         0x04U
#define NRF24L01_CONFIG_PWR_UP       0x02U
#define NRF24L01_CONFIG_PRIM_RX      0x01U

/* ---------------- STATUS 位 ---------------- */
#define NRF24L01_STATUS_RX_DR        0x40U
#define NRF24L01_STATUS_TX_DS        0x20U
#define NRF24L01_STATUS_MAX_RT       0x10U
#define NRF24L01_STATUS_RX_P_NO      0x0EU
#define NRF24L01_STATUS_TX_FULL      0x01U

/* FIFO 空时 RX_P_NO 读值为 7 */
#define NRF24L01_RX_PIPE_EMPTY       7U

/* ---------------- RF_SETUP 位 ---------------- */
#define NRF24L01_RF_SETUP_CONT_WAVE  0x80U
#define NRF24L01_RF_SETUP_PLL_LOCK   0x10U
#define NRF24L01_RF_SETUP_RATE_1MBPS 0x00U
#define NRF24L01_RF_SETUP_RATE_2MBPS 0x08U
#define NRF24L01_RF_SETUP_RATE_250K  0x20U
#define NRF24L01_RF_SETUP_PWR_M18DBM 0x00U
#define NRF24L01_RF_SETUP_PWR_M12DBM 0x02U
#define NRF24L01_RF_SETUP_PWR_M6DBM  0x04U
#define NRF24L01_RF_SETUP_PWR_0DBM   0x06U

/* ---------------- 物理参数 ---------------- */
#define NRF24L01_CHANNEL_MIN         0U
#define NRF24L01_CHANNEL_MAX         125U
#define NRF24L01_FREQ_BASE_KHZ       2400000U
#define NRF24L01_FREQ_STEP_KHZ       1000U
#define NRF24L01_FREQ_MAX_KHZ        (NRF24L01_FREQ_BASE_KHZ + \
                                      (NRF24L01_CHANNEL_MAX * NRF24L01_FREQ_STEP_KHZ))

#define NRF24L01_PAYLOAD_MAX         32U
#define NRF24L01_ADDR_WIDTH_MIN      3U
#define NRF24L01_ADDR_WIDTH_MAX      5U

#define NRF24L01_RX_SETTLE_US        130U   /* RX 模式建立时间（130us） */
#define NRF24L01_CE_PULSE_US         20U    /* CE 至少 10us */

#endif /* NRF24L01_REGISTER_H */
