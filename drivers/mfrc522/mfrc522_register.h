#ifndef MFRC522_REGISTER_H
#define MFRC522_REGISTER_H

/*
 * MFRC522 13.56MHz 读卡器寄存器与命令定义。
 * SPI 地址字节：bit7 = 读标志，bit6:1 = 寄存器地址，bit0 = 0。
 */

#define MFRC522_REG_COMMAND        0x01U
#define MFRC522_REG_COM_IEN        0x02U
#define MFRC522_REG_COM_IRQ        0x04U
#define MFRC522_REG_ERROR          0x06U
#define MFRC522_REG_STATUS2        0x08U
#define MFRC522_REG_FIFO_DATA      0x09U
#define MFRC522_REG_FIFO_LEVEL     0x0AU
#define MFRC522_REG_CONTROL        0x0CU
#define MFRC522_REG_BIT_FRAMING    0x0DU
#define MFRC522_REG_COLL           0x0EU
#define MFRC522_REG_MODE           0x11U
#define MFRC522_REG_TX_CONTROL     0x14U
#define MFRC522_REG_TX_ASK         0x15U
#define MFRC522_REG_CRC_RESULT_H   0x21U
#define MFRC522_REG_CRC_RESULT_L   0x22U
#define MFRC522_REG_RF_CFG         0x26U
#define MFRC522_REG_T_MODE         0x2AU
#define MFRC522_REG_T_PRESCALER    0x2BU
#define MFRC522_REG_T_RELOAD_H     0x2CU
#define MFRC522_REG_T_RELOAD_L     0x2DU
#define MFRC522_REG_VERSION        0x37U

/* CommandReg 命令 */
#define MFRC522_CMD_IDLE           0x00U
#define MFRC522_CMD_CALC_CRC       0x03U
#define MFRC522_CMD_TRANSCEIVE     0x0CU
#define MFRC522_CMD_SOFT_RESET     0x0FU

/* ComIrqReg 位 */
#define MFRC522_IRQ_TIMER          0x01U
#define MFRC522_IRQ_ERR            0x02U
#define MFRC522_IRQ_RX             0x20U
#define MFRC522_IRQ_IDLE           0x10U
#define MFRC522_IRQ_ALL            0x7FU

/* ErrorReg 位 */
#define MFRC522_ERR_PROTOCOL       0x02U
#define MFRC522_ERR_PARITY         0x04U
#define MFRC522_ERR_CRC            0x08U
#define MFRC522_ERR_OVERFLOW       0x10U

/* TxControlReg 位 */
#define MFRC522_TX1_RF_ON          0x01U
#define MFRC522_TX2_RF_ON          0x02U

/* BitFramingReg 位 */
#define MFRC522_BITFRAMING_START   0x80U

/* FIFOLevelReg 位 */
#define MFRC522_FIFO_FLUSH         0x80U

/* ISO14443A 短帧/命令 */
#define MFRC522_PICC_REQA          0x26U
#define MFRC522_PICC_ANTICOLL      0x93U
#define MFRC522_PICC_SELECT        0x93U
#define MFRC522_PICC_HALT          0x50U
#define MFRC522_MIFARE_WRITE       0xA0U
#define MFRC522_ACK                0x0AU

#define MFRC522_UID_MAX            10U
#define MFRC522_FIFO_SIZE          64U
#define MFRC522_TIMEOUT_MS         50U

/* 已知 VersionReg 值：0x91 = MFRC522 v1.0，0x92 = v2.0 */
#define MFRC522_VERSION_1_0        0x91U
#define MFRC522_VERSION_2_0        0x92U

#endif /* MFRC522_REGISTER_H */
