#include "vl6180x.h"
#include "vl6180x_register.h"
#include "log.h"

/*
 * VL6180X 初始化说明（重要）：
 *
 * 器件首次上电时 SYSTEM__FRESH_OUT_OF_RESET(0x0016) 为 1，ST 要求在此状态下
 * 写入一段固定的"私有参数"（Private Settings Register Values，约 70 项，
 * 覆盖 0x0207/0x0208/0x0096/0x00E3... 等），否则测距精度与量程无法保证。
 * 该表由 ST API（VL6180X_InitData / VL6180X_RangeSetMaxConvergenceTime 等）
 * 提供，无法凭数据手册推导。
 *
 * 因此本驱动只实现基础路径：清 FRESH_OUT_OF_RESET 标志、配置 range 中断源、
 * 设置测量间隔，然后单次测距。needs_private_settings 置位表示上层应补充
 * 私有参数（需按 datasheet / ST API 补充）；未补充时读数可用但精度有限。
 */

static int vl6180x_write_u8(vl6180x_handle_t *handle, uint16_t reg, uint8_t value)
{
    return driver_i2c_write_reg16(&handle->i2c, reg, &value, 1U);
}

static int vl6180x_read_u8(vl6180x_handle_t *handle, uint16_t reg, uint8_t *value)
{
    return driver_i2c_read_reg16(&handle->i2c, reg, value, 1U);
}

/* 轮询 RESULT__INTERRUPT_STATUS_GPIO 的 range 新样本位 */
static int vl6180x_wait_range_ready(vl6180x_handle_t *handle)
{
    uint32_t start = driver_tick_ms();
    uint8_t status = 0U;
    int ret;

    for (;;) {
        ret = vl6180x_read_u8(handle, VL6180X_REG_RESULT__INTERRUPT_STATUS_GPIO, &status);
        if (ret != DRIVER_OK) {
            return ret;
        }
        if ((status & VL6180X_INT_STATUS_RANGE_MASK) != 0U) {
            return DRIVER_OK;
        }
        if ((driver_tick_ms() - start) >= VL6180X_RANGE_TIMEOUT_MS) {
            LOG_W("vl6180x", "range timeout, int status=0x%02X", (unsigned)status);
            return (int)HAL_TIMEOUT;
        }
        driver_delay_ms(VL6180X_POLL_INTERVAL_MS);
    }
}

int vl6180x_init(vl6180x_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint8_t id = 0U;
    uint8_t fresh = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : VL6180X_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->needs_private_settings = false;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("vl6180x", "probe failed at 0x%02X", (unsigned)handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    /* ID 校验：model id 必须为 0xB4 */
    ret = vl6180x_read_u8(handle, VL6180X_REG_IDENTIFICATION__MODEL_ID, &id);
    if (ret != DRIVER_OK) {
        LOG_E("vl6180x", "read model id failed: %d", ret);
        return ret;
    }
    if (id != VL6180X_MODEL_ID_VALUE) {
        LOG_E("vl6180x", "unexpected model id 0x%02X", (unsigned)id);
        return (int)HAL_NOT_FOUND;
    }

    /* 首次上电：需要 ST 私有参数，这里只清标志并记录待补状态 */
    ret = vl6180x_read_u8(handle, VL6180X_REG_SYSTEM__FRESH_OUT_OF_RESET, &fresh);
    if (ret == DRIVER_OK && fresh != 0U) {
        handle->needs_private_settings = true;
        (void)vl6180x_write_u8(handle, VL6180X_REG_SYSTEM__FRESH_OUT_OF_RESET,
                               VL6180X_FRESH_OUT_OF_RESET_CLEAR);
        LOG_W("vl6180x", "fresh out of reset: private settings missing "
                         "(supplement per datasheet/ST API)");
    }

    /* 待补：ST Private Settings 批量寄存器序列（需按 datasheet / ST API 移植） */

    ret = vl6180x_write_u8(handle, VL6180X_REG_SYSTEM__INTERRUPT_CONFIG_GPIO,
                           VL6180X_INTERRUPT_CONFIG_RANGE);
    if (ret != DRIVER_OK) {
        LOG_E("vl6180x", "config interrupt failed: %d", ret);
        return ret;
    }

    ret = vl6180x_write_u8(handle, VL6180X_REG_SYSRANGE__INTERMEASUREMENT_PERIOD,
                           VL6180X_INTERMEASUREMENT_PERIOD_MS);
    if (ret != DRIVER_OK) {
        LOG_E("vl6180x", "set intermeasurement period failed: %d", ret);
        return ret;
    }

    handle->initialized = true;
    LOG_I("vl6180x", "init ok");
    return DRIVER_OK;
}

int vl6180x_deinit(vl6180x_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int vl6180x_read(vl6180x_handle_t *handle, vl6180x_data_t *data)
{
    uint8_t raw = 0U;
    uint8_t status = 0U;
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = vl6180x_write_u8(handle, VL6180X_REG_SYSRANGE__START,
                           VL6180X_SYSRANGE_START_SINGLE);
    if (ret != DRIVER_OK) {
        LOG_E("vl6180x", "start ranging failed: %d", ret);
        return ret;
    }

    ret = vl6180x_wait_range_ready(handle);
    if (ret != DRIVER_OK) {
        (void)vl6180x_write_u8(handle, VL6180X_REG_SYSRANGE__START, VL6180X_SYSRANGE_STOP);
        return ret;
    }

    /* 距离为单字节 mm */
    ret = vl6180x_read_u8(handle, VL6180X_REG_RESULT__RANGE_VAL, &raw);
    if (ret != DRIVER_OK) {
        LOG_E("vl6180x", "read range value failed: %d", ret);
        (void)vl6180x_write_u8(handle, VL6180X_REG_SYSRANGE__START, VL6180X_SYSRANGE_STOP);
        return ret;
    }
    data->distance_mm = (uint16_t)raw;

    if (vl6180x_read_u8(handle, VL6180X_REG_RESULT__RANGE_STATUS, &status) != DRIVER_OK) {
        status = 0xFFU;
    }
    data->range_status = (uint8_t)(status & VL6180X_RANGE_STATUS_MASK);

    (void)vl6180x_write_u8(handle, VL6180X_REG_SYSTEM__INTERRUPT_CLEAR,
                           VL6180X_INTERRUPT_CLEAR_ALL);
    (void)vl6180x_write_u8(handle, VL6180X_REG_SYSRANGE__START, VL6180X_SYSRANGE_STOP);

    if (data->range_status != 0U) {
        LOG_D("vl6180x", "range status %u, dist=%umm",
              (unsigned)data->range_status, (unsigned)data->distance_mm);
    }
    return DRIVER_OK;
}

int vl6180x_read_distance(vl6180x_handle_t *handle, uint16_t *distance_mm)
{
    vl6180x_data_t data;
    int ret;

    if (distance_mm == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = vl6180x_read(handle, &data);
    if (ret != DRIVER_OK) {
        return ret;
    }

    *distance_mm = data.distance_mm;
    return (data.range_status == 0U) ? DRIVER_OK : (int)HAL_NOT_READY;
}
