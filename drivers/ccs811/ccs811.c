#include "ccs811.h"
#include "ccs811_register.h"
#include "log.h"

/*
 * CCS811 初始化顺序（ams 数据手册 DS-000148）：
 *   软复位 -> HW_ID 校验(0x81) -> STATUS 检查(ERROR/APP_VALID)
 *   -> APP_START(0xF4) 进入 application 模式 -> MEAS_MODE 选择 1s 周期。
 * APP_START 切换瞬间器件会 NACK，因此失败时延时重试。
 */

static int ccs811_read_u8(ccs811_handle_t *handle, uint8_t reg, uint8_t *value)
{
    return driver_i2c_read_reg(&handle->i2c, reg, value, 1U);
}

static int ccs811_write_u8(ccs811_handle_t *handle, uint8_t reg, uint8_t value)
{
    return driver_i2c_write_reg(&handle->i2c, reg, &value, 1U);
}

static int ccs811_soft_reset(ccs811_handle_t *handle)
{
    uint8_t magic[4];

    magic[0] = CCS811_SW_RESET_MAGIC_0;
    magic[1] = CCS811_SW_RESET_MAGIC_1;
    magic[2] = CCS811_SW_RESET_MAGIC_2;
    magic[3] = CCS811_SW_RESET_MAGIC_3;

    if (driver_i2c_write_reg(&handle->i2c, CCS811_REG_SW_RESET, magic, sizeof(magic)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(CCS811_RESET_DELAY_MS);
    return DRIVER_OK;
}

/* 从 boot 模式切到 application 模式 */
static int ccs811_start_app(ccs811_handle_t *handle)
{
    uint8_t status = 0U;
    int ret;

    /* 数据手册形式：APP_START 写 0 字节数据 */
    ret = driver_i2c_write_reg(&handle->i2c, CCS811_REG_APP_START, NULL, 0U);
    if (ret != DRIVER_OK) {
        /* 端口层不接受 0 长度写时，退回写 1 字节（兼容旧实现） */
        uint8_t legacy = CCS811_APP_START_LEGACY_DATA;

        ret = ccs811_write_u8(handle, CCS811_REG_APP_START, legacy);
    }
    if (ret != DRIVER_OK) {
        LOG_E("ccs811", "app start write failed: %d", ret);
        return ret;
    }
    driver_delay_ms(CCS811_APP_START_DELAY_MS);

    ret = ccs811_read_u8(handle, CCS811_REG_STATUS, &status);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if ((status & CCS811_STATUS_FW_MODE) == 0U) {
        LOG_E("ccs811", "not in app mode, status=0x%02X", (unsigned)status);
        return (int)IOT_HW_ERROR;
    }
    return DRIVER_OK;
}

/* 轮询 STATUS.DATA_READY */
static int ccs811_wait_data_ready(ccs811_handle_t *handle)
{
    uint32_t start = driver_tick_ms();
    uint8_t status = 0U;
    int ret;

    for (;;) {
        ret = ccs811_read_u8(handle, CCS811_REG_STATUS, &status);
        if (ret != DRIVER_OK) {
            return ret;
        }
        if ((status & CCS811_STATUS_ERROR) != 0U) {
            uint8_t err = 0U;

            (void)ccs811_read_u8(handle, CCS811_REG_ERROR_ID, &err);
            LOG_E("ccs811", "sensor error, ERROR_ID=0x%02X", (unsigned)err);
            return (int)IOT_HW_ERROR;
        }
        if ((status & CCS811_STATUS_DATA_READY) != 0U) {
            return DRIVER_OK;
        }
        if ((driver_tick_ms() - start) >= handle->ready_timeout_ms) {
            LOG_W("ccs811", "data not ready, status=0x%02X", (unsigned)status);
            return (int)IOT_TIMEOUT;
        }
        driver_delay_ms(CCS811_POLL_INTERVAL_MS);
    }
}

int ccs811_init(ccs811_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t id = 0U;
    uint8_t status = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : CCS811_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->ready_timeout_ms = CCS811_READY_TIMEOUT_MS;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("ccs811", "probe failed at 0x%02X", (unsigned)handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    if (ccs811_soft_reset(handle) != DRIVER_OK) {
        LOG_W("ccs811", "soft reset failed, continue");
    }

    /* HW_ID 校验：必须为 0x81 */
    ret = ccs811_read_u8(handle, CCS811_REG_HW_ID, &id);
    if (ret != DRIVER_OK) {
        LOG_E("ccs811", "read hw id failed: %d", ret);
        return ret;
    }
    if (id != CCS811_HW_ID_VALUE) {
        LOG_E("ccs811", "unexpected hw id 0x%02X", (unsigned)id);
        return (int)IOT_NOT_FOUND;
    }

    ret = ccs811_read_u8(handle, CCS811_REG_STATUS, &status);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if ((status & CCS811_STATUS_ERROR) != 0U) {
        uint8_t err = 0U;

        (void)ccs811_read_u8(handle, CCS811_REG_ERROR_ID, &err);
        LOG_E("ccs811", "status error, ERROR_ID=0x%02X", (unsigned)err);
        return (int)IOT_HW_ERROR;
    }
    if ((status & CCS811_STATUS_APP_VALID) == 0U) {
        LOG_E("ccs811", "no valid app firmware (bootloader only)");
        return (int)IOT_NOT_READY;
    }

    if ((status & CCS811_STATUS_FW_MODE) == 0U) {
        ret = ccs811_start_app(handle);
        if (ret != DRIVER_OK) {
            return ret;
        }
    }

    /* mode 1：每 1s 一次测量，关闭中断与阈值 */
    ret = ccs811_write_u8(handle, CCS811_REG_MEAS_MODE,
                          (uint8_t)(CCS811_MEAS_MODE_1S & CCS811_MEAS_MODE_MASK));
    if (ret != DRIVER_OK) {
        LOG_E("ccs811", "set meas mode failed: %d", ret);
        return ret;
    }
    driver_delay_ms(CCS811_APP_START_DELAY_MS);

    handle->initialized = true;
    LOG_I("ccs811", "init ok");
    return DRIVER_OK;
}

int ccs811_deinit(ccs811_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int ccs811_read(ccs811_handle_t *handle, ccs811_data_t *data)
{
    uint8_t raw[CCS811_RESULT_LEN];
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = ccs811_wait_data_ready(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 0x02 起 8 字节：eCO2(2) + TVOC(2) + status + error_id + raw(2) */
    ret = driver_i2c_read_reg(&handle->i2c, CCS811_REG_ALG_RESULT_DATA, raw, sizeof(raw));
    if (ret != DRIVER_OK) {
        LOG_E("ccs811", "read result failed: %d", ret);
        return ret;
    }

    data->co2_ppm = (uint16_t)driver_be16(&raw[0]);
    data->tvoc_ppb = (uint16_t)driver_be16(&raw[2]);
    data->error_id = raw[5];
    return DRIVER_OK;
}

int ccs811_set_environment(ccs811_handle_t *handle, float humidity_rh, float temperature_c)
{
    uint8_t buf[4];
    uint16_t hum;
    uint16_t temp;
    float rh = humidity_rh;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    if (rh < 0.0f) {
        rh = 0.0f;
    } else if (rh > 100.0f) {
        rh = 100.0f;
    }

    hum = (uint16_t)(rh * CCS811_ENV_SCALE + 0.5f);
    temp = (uint16_t)((temperature_c + CCS811_ENV_TEMP_OFFSET) * CCS811_ENV_SCALE + 0.5f);

    buf[0] = (uint8_t)(hum >> 8);
    buf[1] = (uint8_t)(hum & 0xFFU);
    buf[2] = (uint8_t)(temp >> 8);
    buf[3] = (uint8_t)(temp & 0xFFU);
    return driver_i2c_write_reg(&handle->i2c, CCS811_REG_ENV_DATA, buf, sizeof(buf));
}

int ccs811_read_error(ccs811_handle_t *handle, uint8_t *error_id)
{
    if (handle == NULL || error_id == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return ccs811_read_u8(handle, CCS811_REG_ERROR_ID, error_id);
}
