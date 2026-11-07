#ifndef SX1262_REGISTER_H
#define SX1262_REGISTER_H

/*
 * SX1262 / SX1268 / LLCC68 LoRa 收发器命令字定义。
 * 参考 Semtech SX1261/2 datasheet rev 2.1 第 13 章命令表。
 * SPI 时序：拉低 NSS -> 发送 opcode + 参数 -> 释放 NSS；
 * 读命令在 opcode 后紧跟 1 个 dummy/status 字节，再读数据。
 */

#define SX126X_SET_SLEEP               0x84U
#define SX126X_SET_STANDBY             0x80U
#define SX126X_SET_FS                  0xC1U
#define SX126X_SET_TX                  0x83U
#define SX126X_SET_RX                  0x82U
#define SX126X_STOP_TIMER_ON_PREAMBLE  0x9FU
#define SX126X_SET_RX_DUTY_CYCLE       0x94U
#define SX126X_SET_CAD                 0xC5U
#define SX126X_SET_TX_CONT_WAVE        0xD1U
#define SX126X_SET_TX_INFINITE_PREAMBLE 0xD2U
#define SX126X_SET_REGULATOR_MODE      0x96U
#define SX126X_CALIBRATE_IMAGE         0x98U
#define SX126X_SET_PA_CONFIG           0x95U
#define SX126X_SET_RX_TX_FALLBACK       0x93U
#define SX126X_WRITE_REGISTER          0x0DU
#define SX126X_READ_REGISTER           0x1DU
#define SX126X_WRITE_BUFFER            0x0EU
#define SX126X_READ_BUFFER             0x1EU
#define SX126X_SET_DIO_IRQ_PARAMS      0x08U
#define SX126X_GET_IRQ_STATUS          0x12U
#define SX126X_CLEAR_IRQ_STATUS        0x02U
#define SX126X_SET_DIO2_AS_RF_SWITCH   0x9DU
#define SX126X_SET_DIO3_AS_TCXO_CTRL   0x97U
#define SX126X_SET_RF_FREQUENCY        0x86U
#define SX126X_SET_PACKET_TYPE         0x8AU
#define SX126X_GET_PACKET_TYPE         0x11U
#define SX126X_SET_TX_PARAMS           0x8EU
#define SX126X_SET_MODULATION_PARAMS   0x8BU
#define SX126X_SET_PACKET_PARAMS       0x8CU
#define SX126X_SET_CAD_PARAMS          0x88U
#define SX126X_SET_BUFFER_BASE_ADDR    0x8FU
#define SX126X_SET_LORA_SYMB_TIMEOUT   0xA0U
#define SX126X_GET_STATUS              0xC0U
#define SX126X_GET_RX_BUFFER_STATUS    0x13U
#define SX126X_GET_PACKET_STATUS       0x14U
#define SX126X_GET_DEVICE_ERRORS       0x17U
#define SX126X_GET_STATS               0x10U

/* SetStandby 模式 */
#define SX126X_STDBY_RC                0x00U
#define SX126X_STDBY_XOSC              0x01U

/* SetPacketType */
#define SX126X_PACKET_TYPE_GFSK        0x00U
#define SX126X_PACKET_TYPE_LORA        0x01U

/* SetRegulatorMode */
#define SX126X_REGULATOR_LDO           0x00U
#define SX126X_REGULATOR_DC_DC         0x01U

/* IRQ 位（2 字节，小端返回） */
#define SX126X_IRQ_TX_DONE             0x0001U
#define SX126X_IRQ_RX_DONE             0x0002U
#define SX126X_IRQ_PREAMBLE_DETECTED   0x0004U
#define SX126X_IRQ_SYNC_WORD_VALID     0x0008U
#define SX126X_IRQ_HEADER_VALID        0x0010U
#define SX126X_IRQ_HEADER_ERR          0x0020U
#define SX126X_IRQ_CRC_ERR             0x0040U
#define SX126X_IRQ_CAD_DONE            0x0080U
#define SX126X_IRQ_CAD_DETECTED        0x0100U
#define SX126X_IRQ_TIMEOUT             0x0200U

#define SX126X_IRQ_RX_ALL \
    (SX126X_IRQ_RX_DONE | SX126X_IRQ_CRC_ERR | SX126X_IRQ_TIMEOUT)

/* 带宽编码（SetModulationParams 第 2 字节） */
#define SX126X_BW_125KHZ               0x04U
#define SX126X_BW_250KHZ               0x05U
#define SX126X_BW_500KHZ               0x06U

/* 编码率：1..4 -> 4/5..4/8 */
#define SX126X_CR_4_5                  0x01U
#define SX126X_CR_4_8                  0x04U

/* PA ramp time：0x04 = 200us */
#define SX126X_PA_RAMP_200US           0x04U

/* 频率范围（Hz） */
#define SX1262_FREQ_MIN_HZ             150000000U
#define SX1262_FREQ_MAX_HZ             960000000U
#define SX1268_FREQ_MIN_HZ             410000000U
#define SX1268_FREQ_MAX_HZ             810000000U
#define LLCC68_FREQ_MIN_HZ             150000000U
#define LLCC68_FREQ_MAX_HZ             960000000U

/* 晶振 32MHz，频率字 = f * 2^25 / 32e6 */
#define SX126X_XTAL_FREQ_HZ            32000000U
#define SX126X_FREQ_MUL                (1UL << 25)

#define SX126X_PAYLOAD_MAX             255U
#define SX126X_TX_TIMEOUT_UNIT_US      15.625f

#endif /* SX1262_REGISTER_H */
