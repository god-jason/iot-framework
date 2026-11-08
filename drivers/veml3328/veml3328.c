#include "veml3328.h"
#include "veml3328_register.h"
#include "log.h"

#define VEML3328_CONF_CHECK_MASK (VEML3328_CONF_SD | VEML3328_CONF_AF_MASK | \
                                  VEML3328_CONF_DG_MASK | VEML3328_CONF_GAIN_MASK | \
                                  VEML3328_CONF_IT_MASK)

/* VEML3328 的寄存器数据为 16 位，低字节在前（与通道数据排列一致） */
static int veml3328_write_conf(veml3328_handle_t *handle, uint16_t conf)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(conf & 0xFFU);
    buf[1] = (uint8_t)(conf >> 8);
    return (int)driver_i2c_write_reg(&handle->i2c, VEML3328_REG_CONF, buf, sizeof(buf));
}

static int veml3328_read_word(veml3328_handle_t *handle, uint8_t reg, uint16_t *value)
{
    uint8_t buf[2];
    int ret;

    ret = (int)driver_i2c_read_reg(&handle->i2c, reg, buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        return ret;
    }
    *value = (uint16_t)(((uint16_t)buf[1] << 8) | (uint16_t)buf[0]);
    return DRIVER_OK;
}

int veml3328_init(veml3328_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint16_t conf = 0U;
    uint16_t id = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : VEML3328_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("veml3328", "probe failed at 0x%02X", (unsigned)handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    ret = veml3328_write_conf(handle, VEML3328_CONF_DEFAULT);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 回读校验：确认 SD/IT/增益位确实写入（写通路自检） */
    ret = veml3328_read_word(handle, VEML3328_REG_CONF, &conf);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if ((conf & VEML3328_CONF_CHECK_MASK) !=
        (VEML3328_CONF_DEFAULT & VEML3328_CONF_CHECK_MASK)) {
        LOG_E("veml3328", "conf readback 0x%04X != 0x%04X",
              (unsigned)conf, (unsigned)VEML3328_CONF_DEFAULT);
        return (int)HAL_HW_ERROR;
    }

    /* 首次转换需要完整积分时间 */
    driver_delay_ms(VEML3328_MEASURE_DELAY_MS);

    /* ID 仅用于调试：部分兼容片不实现该寄存器，不作为失败条件 */
    if (veml3328_read_word(handle, VEML3328_REG_ID, &id) == DRIVER_OK &&
        id != VEML3328_ID_VALUE) {
        LOG_D("veml3328", "id 0x%04X (datasheet 0x%04X)",
              (unsigned)id, (unsigned)VEML3328_ID_VALUE);
    }

    handle->initialized = true;
    LOG_I("veml3328", "init ok at 0x%02X", (unsigned)handle->i2c.address);
    return DRIVER_OK;
}

int veml3328_deinit(veml3328_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized && handle->i2c.handle != NULL) {
        /* SD = 1 关断，降低功耗；下次 init 会重新写配置 */
        (void)veml3328_write_conf(handle, VEML3328_CONF_SD);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int veml3328_get_conf(veml3328_handle_t *handle, uint16_t *conf)
{
    if (handle == NULL || conf == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return veml3328_read_word(handle, VEML3328_REG_CONF, conf);
}

int veml3328_read_raw(veml3328_handle_t *handle, uint16_t counts[VEML3328_CHANNEL_COUNT])
{
    uint8_t data[VEML3328_DATA_LEN];
    size_t i;
    int ret;

    if (handle == NULL || counts == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    /* 单次块读 5 个通道，保证同一采样时刻的一致性 */
    ret = (int)driver_i2c_read_reg(&handle->i2c, VEML3328_REG_DATA_START,
                                   data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }

    for (i = 0U; i < VEML3328_CHANNELS; i++) {
        counts[i] = (uint16_t)(((uint16_t)data[i * 2U + 1U] << 8) | (uint16_t)data[i * 2U]);
    }
    return DRIVER_OK;
}

int veml3328_read(veml3328_handle_t *handle, veml3328_data_t *data)
{
    uint16_t counts[VEML3328_CHANNEL_COUNT];
    int32_t ambient;
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = veml3328_read_raw(handle, counts);
    if (ret != DRIVER_OK) {
        return ret;
    }

    data->clear = counts[VEML3328_CHANNEL_CLEAR];
    data->red = counts[VEML3328_CHANNEL_RED];
    data->green = counts[VEML3328_CHANNEL_GREEN];
    data->blue = counts[VEML3328_CHANNEL_BLUE];
    data->ir = counts[VEML3328_CHANNEL_IR];

    /* 环境光用 G - IR 近似去红外分量（IR 在绿通道上贡献最大），
     * 系数为标称值，含义与限制见 veml3328_register.h。 */
    ambient = (int32_t)data->green - (int32_t)data->ir;
    if (ambient < 0) {
        ambient = 0;
    }
    data->lux = (float)ambient * VEML3328_LUX_PER_COUNT;
    return DRIVER_OK;
}
