#ifndef LAN8720_H
#define LAN8720_H

/*
 * SMSC LAN8720 / LAN8720A 10/100 以太网 PHY 驱动（RMII + MDIO）。
 *
 * 参考 libdriver 风格：
 *   lan8720_init / lan8720_deinit / lan8720_read_reg / lan8720_write_reg
 *   lan8720_link_status
 *
 * 说明：
 *   - 数据通路（RMII）由 MAC/hal_eth 负责，本驱动只管理 PHY 的 MDIO 寄存器。
 *   - MDIO 采用 GPIO 位操作（MDC + 双向 MDIO），符合 IEEE 802.3 Clause 22 帧格式；
 *     MDC 频率约 250kHz（远低于 2.5MHz 上限），无需专用 MAC 的 MDIO 外设。
 *   - 部分扩展功能（中断引脚、WOL、节能模式）需按 datasheet/固件补充。
 */

#include "common/driver_io.h"
#include "lan8720_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool link_up;
    bool full_duplex;
    bool auto_negotiation_done;
    uint8_t speed_mbps;     /* 10 / 100，未连接时为 0 */
} lan8720_link_t;

typedef struct {
    hal_pin_t mdc;
    hal_pin_t mdio;
    uint8_t phy_address;
    bool initialized;
} lan8720_handle_t;

/**
 * @brief  初始化 MDIO 引脚并校验 PHY ID
 * @param  mdc         MDC 时钟引脚（推挽输出）
 * @param  mdio        MDIO 数据引脚（双向，需要上拉）
 * @param  phy_address PHY 地址 0..31，常用 0x00 / 0x01
 */
int lan8720_init(lan8720_handle_t *handle, hal_pin_t mdc, hal_pin_t mdio,
                 uint8_t phy_address);

int lan8720_deinit(lan8720_handle_t *handle);

/** Clause 22 读寄存器 */
int lan8720_read_reg(lan8720_handle_t *handle, uint8_t reg, uint16_t *value);

/** Clause 22 写寄存器 */
int lan8720_write_reg(lan8720_handle_t *handle, uint8_t reg, uint16_t value);

/** 读取 PHY 标识（PHYID1/PHYID2） */
int lan8720_read_id(lan8720_handle_t *handle, uint16_t *id1, uint16_t *id2);

/** 软复位 PHY（BMCR.RESET），等待自清零 */
int lan8720_reset(lan8720_handle_t *handle);

/** 读取链路状态：link / 速率 / 双工 */
int lan8720_link_status(lan8720_handle_t *handle, lan8720_link_t *link);

/** 使能/关闭低功耗（BMCR.POWER_DOWN） */
int lan8720_set_power_down(lan8720_handle_t *handle, bool enable);

/** 重启自协商（BMCR.RESTART_AN） */
int lan8720_restart_auto_negotiation(lan8720_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* LAN8720_H */
