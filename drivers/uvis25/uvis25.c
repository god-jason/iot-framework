#include "uvis25.h"
#include "uvis25_register.h"
#include "log.h"

static uint8_t uvis25_odr_code(uvis25_odr_t odr)
{
    switch (odr) {
    case UVIS25_ODR_7HZ:
        return UVIS25_ODR_CODE_7HZ;
    case UVIS25_ODR_12HZ:
        return UVIS25_ODR_CODE_12HZ;
    case UVIS25_ODR_1HZ:
    default:
        return UVIS25_ODR_CODE_1HZ;
    }
}

/* PD 位清零表示退出掉电；BDU 保证读 16 位输出期间数据不被刷新 */
static uint8_t uvis25_ctrl1_value(const uvis25_handle_t *handle)
{
    return (uint8_t)(((uint8_t)uvis25_odr_code(handle->odr) << UVIS25_CTRL1_ODR_SHIFT) |
                     UVIS25_CTRL1_BDU);
}

static int uvis25_write_reg(uvis25_handle_t *handle, uint8_t reg, uint8_t value)
{
    return driver_i2c_write_reg(&handle->i2c, reg, &value, 1U);
}

/* 轮询数据就绪标志，超时返回 PORT_TIMEOUT */
static int uvis25_wait_data_ready(uvis25_handle_t *handle)
{
    uint32_t start = driver_tick_ms();
    uint8_t status = 0U;

    for (;;) {
        if (driver_i2c_read_reg(&handle->i2c, UVIS25_REG_STATUS, &status, 1U) != DRIVER_OK) {
            return (int)PORT_IO_ERROR;
        }
        if ((status & UVIS25_STATUS_UV_DA) != 0U) {
            return DRIVER_OK;
        }
        if ((uint32_t)(driver_tick_ms() - start) >= UVIS25_POLL_TIMEOUT_MS) {
            return (int)PORT_TIMEOUT;
        }
        driver_delay_ms(20U);
    }
}

int uvis25_init(uvis25_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    uint8_t who = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : UVIS25_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->odr = UVIS25_ODR_1HZ;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("uvis25", "probe failed at 0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    /* WHO_AM_I 校验：不匹配时仅告警，寄存器映射需按所用 datasheet 复核 */
    ret = driver_i2c_read_reg(&handle->i2c, UVIS25_REG_WHO_AM_I, &who, 1U);
    if (ret == DRIVER_OK && who != UVIS25_WHO_AM_I_VALUE) {
        LOG_W("uvis25", "unexpected who_am_i 0x%02X (expect 0x%02X)", who,
              UVIS25_WHO_AM_I_VALUE);
    }

    if (uvis25_write_reg(handle, UVIS25_REG_CTRL1, uvis25_ctrl1_value(handle)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    driver_delay_ms(UVIS25_POWER_ON_DELAY_MS);

    handle->initialized = true;
    LOG_I("uvis25", "init ok at 0x%02X (who=0x%02X)", handle->i2c.address, who);
    return DRIVER_OK;
}

int uvis25_deinit(uvis25_handle_t *handle)
{
    uint8_t value = (uint8_t)(UVIS25_CTRL1_PD | UVIS25_CTRL1_BDU);

    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->i2c.handle != NULL) {
        (void)uvis25_write_reg(handle, UVIS25_REG_CTRL1, value);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int uvis25_set_odr(uvis25_handle_t *handle, uvis25_odr_t odr)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if ((unsigned int)odr > (unsigned int)UVIS25_ODR_12HZ) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->odr = odr;
    if (uvis25_write_reg(handle, UVIS25_REG_CTRL1, uvis25_ctrl1_value(handle)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    driver_delay_ms(UVIS25_POWER_ON_DELAY_MS);
    return DRIVER_OK;
}

int uvis25_read_raw(uvis25_handle_t *handle, uint16_t *raw)
{
    uint8_t buf[2] = { 0U, 0U };
    int ret;

    if (handle == NULL || raw == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = uvis25_wait_data_ready(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    ret = driver_i2c_read_reg(&handle->i2c, UVIS25_REG_UV_INDEX_L, buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 输出寄存器为小端：先低字节后高字节 */
    *raw = (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
    return DRIVER_OK;
}

int uvis25_read(uvis25_handle_t *handle, float *uv_index)
{
    uint16_t raw = 0U;
    int ret;

    if (uv_index == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = uvis25_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }
    /* 换算比例需按 datasheet / 标定确认 */
    *uv_index = (float)raw / UVIS25_RAW_PER_UVI;
    return DRIVER_OK;
}

int uvis25_get_who_am_i(uvis25_handle_t *handle, uint8_t *who_am_i)
{
    if (handle == NULL || who_am_i == NULL || handle->i2c.handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    return driver_i2c_read_reg(&handle->i2c, UVIS25_REG_WHO_AM_I, who_am_i, 1U);
}
