#include "lan8720.h"
#include "log.h"

#define LAN8720_TAG "lan8720"
#define LAN8720_TRY(expr)           \
    do {                            \
        int r_ = (expr);            \
        if (r_ != DRIVER_OK) {      \
            return r_;              \
        }                           \
    } while (0)

static int lan8720_check(const lan8720_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return DRIVER_OK;
}

/* ---------------- MDIO 位操作（IEEE 802.3 Clause 22） ---------------- */

static void lan8720_mdio_drive(const lan8720_handle_t *handle, int level)
{
    (void)driver_gpio_write(handle->mdio, (level != 0) ? IOT_GPIO_HIGH : IOT_GPIO_LOW);
}

static void lan8720_mdc_pulse(const lan8720_handle_t *handle)
{
    (void)driver_gpio_write(handle->mdc, IOT_GPIO_HIGH);
    driver_delay_us(LAN8720_MDIO_TURNAROUND_US);
    (void)driver_gpio_write(handle->mdc, IOT_GPIO_LOW);
    driver_delay_us(LAN8720_MDIO_TURNAROUND_US);
}

/* 写一个 bit：数据在 MDC 上升沿被 PHY 采样 */
static void lan8720_write_bit(const lan8720_handle_t *handle, uint8_t bit)
{
    lan8720_mdio_drive(handle, (bit != 0U) ? 1 : 0);
    lan8720_mdc_pulse(handle);
}

/* 读一个 bit：MDC 下降沿后 PHY 更新数据，上升沿采样 */
static uint8_t lan8720_read_bit(const lan8720_handle_t *handle)
{
    int level;

    (void)driver_gpio_write(handle->mdc, IOT_GPIO_LOW);
    driver_delay_us(LAN8720_MDIO_TURNAROUND_US);
    level = driver_gpio_read(handle->mdio);
    (void)driver_gpio_write(handle->mdc, IOT_GPIO_HIGH);
    driver_delay_us(LAN8720_MDIO_TURNAROUND_US);
    return (level > 0) ? 1U : 0U;
}

/* 帧头：32 位前导 + ST + OP + PHYAD + REGAD */
static void lan8720_send_header(const lan8720_handle_t *handle, uint8_t op, uint8_t reg)
{
    uint8_t i;

    (void)driver_gpio_output(handle->mdio, IOT_GPIO_HIGH);
    for (i = 0U; i < LAN8720_MDIO_PREAMBLE_BITS; i++) {
        lan8720_write_bit(handle, 1U);
    }
    /* ST = 01，随后 2 位操作码（10 读 / 01 写） */
    lan8720_write_bit(handle, 0U);
    lan8720_write_bit(handle, 1U);
    lan8720_write_bit(handle, (uint8_t)((op >> 1) & 0x01U));
    lan8720_write_bit(handle, (uint8_t)(op & 0x01U));
    for (i = 0U; i < 5U; i++) {
        lan8720_write_bit(handle, (uint8_t)((handle->phy_address >> (4U - i)) & 0x01U));
    }
    for (i = 0U; i < 5U; i++) {
        lan8720_write_bit(handle, (uint8_t)((reg >> (4U - i)) & 0x01U));
    }
}

int lan8720_read_reg(lan8720_handle_t *handle, uint8_t reg, uint16_t *value)
{
    uint16_t data = 0U;
    uint8_t i;
    int ret = lan8720_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (value == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    lan8720_send_header(handle, LAN8720_MDIO_OP_READ, reg);
    /* 转向：释放 MDIO，PHY 输出数据 */
    (void)driver_gpio_input(handle->mdio, IOT_GPIO_PULLUP);
    (void)lan8720_read_bit(handle); /* TA bit */
    for (i = 0U; i < 16U; i++) {
        data = (uint16_t)((data << 1) | lan8720_read_bit(handle));
    }
    (void)driver_gpio_output(handle->mdio, IOT_GPIO_HIGH);

    *value = data;
    return DRIVER_OK;
}

int lan8720_write_reg(lan8720_handle_t *handle, uint8_t reg, uint16_t value)
{
    uint8_t i;
    int ret = lan8720_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }

    lan8720_send_header(handle, LAN8720_MDIO_OP_WRITE, reg);
    /* TA：10 */
    lan8720_write_bit(handle, 1U);
    lan8720_write_bit(handle, 0U);
    for (i = 0U; i < 16U; i++) {
        lan8720_write_bit(handle, (uint8_t)((value >> (15U - i)) & 0x01U));
    }
    return DRIVER_OK;
}

int lan8720_read_id(lan8720_handle_t *handle, uint16_t *id1, uint16_t *id2)
{
    if (id1 == NULL || id2 == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    LAN8720_TRY(lan8720_read_reg(handle, LAN8720_REG_PHYID1, id1));
    return lan8720_read_reg(handle, LAN8720_REG_PHYID2, id2);
}

int lan8720_init(lan8720_handle_t *handle, iot_pin_t mdc, iot_pin_t mdio,
                 uint8_t phy_address)
{
    uint16_t id1 = 0U;
    uint16_t id2 = 0U;
    int ret;

    if (handle == NULL || mdc == 0U || mdio == 0U || mdc == mdio) {
        return (int)IOT_INVALID_PARAM;
    }
    if (phy_address > LAN8720_PHY_ADDRESS_MAX) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->mdc = mdc;
    handle->mdio = mdio;
    handle->phy_address = phy_address;
    handle->initialized = false;

    if (driver_gpio_output(mdc, IOT_GPIO_LOW) != DRIVER_OK ||
        driver_gpio_output(mdio, IOT_GPIO_HIGH) != DRIVER_OK) {
        LOG_E(LAN8720_TAG, "mdio gpio init failed");
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(1U);

    handle->initialized = true;
    ret = lan8720_read_id(handle, &id1, &id2);
    if (ret != DRIVER_OK) {
        handle->initialized = false;
        return ret;
    }
    if (id1 != LAN8720_PHYID1_VALUE ||
        (id2 & LAN8720_PHYID2_MODEL_MASK) != LAN8720_PHYID2_MODEL_VALUE) {
        LOG_W(LAN8720_TAG, "unexpected phy id 0x%04X 0x%04X", (unsigned)id1, (unsigned)id2);
    }

    LOG_I(LAN8720_TAG, "init ok, phy 0x%02X, id 0x%04X 0x%04X",
          handle->phy_address, (unsigned)id1, (unsigned)id2);
    return DRIVER_OK;
}

int lan8720_deinit(lan8720_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)lan8720_set_power_down(handle, true);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int lan8720_reset(lan8720_handle_t *handle)
{
    uint16_t bmcr = 0U;
    uint32_t start;
    int ret = lan8720_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }

    LAN8720_TRY(lan8720_write_reg(handle, LAN8720_REG_BMCR, LAN8720_BMCR_RESET));
    start = driver_tick_ms();
    for (;;) {
        driver_delay_ms(10U);
        LAN8720_TRY(lan8720_read_reg(handle, LAN8720_REG_BMCR, &bmcr));
        if ((bmcr & LAN8720_BMCR_RESET) == 0U) {
            return DRIVER_OK;
        }
        if ((uint32_t)(driver_tick_ms() - start) > LAN8720_RESET_TIMEOUT_MS) {
            return (int)IOT_TIMEOUT;
        }
    }
}

int lan8720_link_status(lan8720_handle_t *handle, lan8720_link_t *link)
{
    uint16_t bmsr = 0U;
    uint16_t bscsr = 0U;
    int ret = lan8720_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (link == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    /* BMSR 的 link 位为 latching-low，连续读两次以获得稳定状态 */
    LAN8720_TRY(lan8720_read_reg(handle, LAN8720_REG_BMSR, &bmsr));
    LAN8720_TRY(lan8720_read_reg(handle, LAN8720_REG_BMSR, &bmsr));

    link->link_up = ((bmsr & LAN8720_BMSR_LINK_UP) != 0U);
    link->auto_negotiation_done = ((bmsr & LAN8720_BMSR_AN_COMPLETE) != 0U);
    if (!link->link_up) {
        link->speed_mbps = 0U;
        link->full_duplex = false;
        return DRIVER_OK;
    }

    LAN8720_TRY(lan8720_read_reg(handle, LAN8720_REG_BSCSR, &bscsr));
    link->speed_mbps = ((bscsr & LAN8720_BSCSR_SPEED_100) != 0U) ? 100U : 10U;
    link->full_duplex = ((bscsr & LAN8720_BSCSR_DUPLEX_FULL) != 0U);
    return DRIVER_OK;
}

int lan8720_set_power_down(lan8720_handle_t *handle, bool enable)
{
    uint16_t bmcr = 0U;
    int ret = lan8720_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }

    LAN8720_TRY(lan8720_read_reg(handle, LAN8720_REG_BMCR, &bmcr));
    if (enable) {
        bmcr |= LAN8720_BMCR_POWER_DOWN;
    } else {
        bmcr = (uint16_t)(bmcr & (uint16_t)~LAN8720_BMCR_POWER_DOWN);
    }
    LAN8720_TRY(lan8720_write_reg(handle, LAN8720_REG_BMCR, bmcr));
    driver_delay_ms(1U);
    return DRIVER_OK;
}

int lan8720_restart_auto_negotiation(lan8720_handle_t *handle)
{
    uint16_t bmcr = 0U;
    int ret = lan8720_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }

    LAN8720_TRY(lan8720_read_reg(handle, LAN8720_REG_BMCR, &bmcr));
    bmcr |= (uint16_t)(LAN8720_BMCR_AUTONEG_EN | LAN8720_BMCR_RESTART_AN);
    bmcr = (uint16_t)(bmcr & (uint16_t)~LAN8720_BMCR_POWER_DOWN);
    return lan8720_write_reg(handle, LAN8720_REG_BMCR, bmcr);
}
