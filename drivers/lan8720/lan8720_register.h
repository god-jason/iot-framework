#ifndef LAN8720_REGISTER_H
#define LAN8720_REGISTER_H

/* SMSC LAN8720 / LAN8720A 10/100 以太网 PHY 标准 MDIO 寄存器定义 */

/* PHY 地址（由 PHYAD0 引脚 / 板级布线决定，RMII 常用 0x00 或 0x01） */
#define LAN8720_PHY_ADDRESS_DEFAULT   0x00U
#define LAN8720_PHY_ADDRESS_MAX       0x1FU

/* 标准寄存器 */
#define LAN8720_REG_BMCR              0x00U  /* Basic Mode Control */
#define LAN8720_REG_BMSR              0x01U  /* Basic Mode Status */
#define LAN8720_REG_PHYID1            0x02U
#define LAN8720_REG_PHYID2            0x03U
#define LAN8720_REG_ANAR              0x04U  /* Auto-Negotiation Advertisement */
#define LAN8720_REG_ANLPAR            0x05U  /* Auto-Negotiation Link Partner Ability */
#define LAN8720_REG_ANER              0x06U  /* Auto-Negotiation Expansion */
#define LAN8720_REG_MCSR              0x11U  /* Mode Control/Status */
#define LAN8720_REG_SMCSR             0x12U  /* Special Modes */
#define LAN8720_REG_BSCSR             0x1FU  /* Special Control/Status（速度/双工状态） */

/* BMCR 位 */
#define LAN8720_BMCR_RESET            0x8000U
#define LAN8720_BMCR_LOOPBACK         0x4000U
#define LAN8720_BMCR_SPEED_100        0x2000U
#define LAN8720_BMCR_AUTONEG_EN       0x1000U
#define LAN8720_BMCR_POWER_DOWN       0x0800U
#define LAN8720_BMCR_ISOLATE          0x0400U
#define LAN8720_BMCR_RESTART_AN       0x0200U
#define LAN8720_BMCR_DUPLEX_FULL      0x0100U

/* BMSR 位 */
#define LAN8720_BMSR_LINK_UP          0x0004U  /* 1: link up（latching low） */
#define LAN8720_BMSR_AN_COMPLETE      0x0020U
#define LAN8720_BMSR_EXT_CAPABLE      0x0001U

/* PHYSID：LAN8720A = 0x0007 / 0xC0F1 */
#define LAN8720_PHYID1_VALUE          0x0007U
#define LAN8720_PHYID2_MODEL_MASK     0xFFF0U
#define LAN8720_PHYID2_MODEL_VALUE    0xC0F0U

/* BSCSR 位（LAN8720 特殊状态寄存器） */
#define LAN8720_BSCSR_SPEED_100       0x0004U  /* 1: 100Mbps, 0: 10Mbps */
#define LAN8720_BSCSR_DUPLEX_FULL     0x0010U  /* 1: 全双工 */

/* MDIO 帧结构 */
#define LAN8720_MDIO_PREAMBLE_BITS    32U
#define LAN8720_MDIO_OP_READ          0x02U    /* Clause 22 读操作码 10 */
#define LAN8720_MDIO_OP_WRITE         0x01U    /* Clause 22 写操作码 01 */
#define LAN8720_MDIO_TURNAROUND_US    1U

/* 复位等待 */
#define LAN8720_RESET_TIMEOUT_MS      500U
#define LAN8720_LINK_POLL_MS          50U

#endif /* LAN8720_REGISTER_H */
