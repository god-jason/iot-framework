#include "vl53l1x.h"
#include "vl53l1x_register.h"
#include "log.h"

/*
 * VL53L1X 初始化说明（重要）：
 *
 * ST 官方 API 的完整流程为：
 *   1) VL53L1X_Reset()                              —— 软复位；
 *   2) VL53L1X_BootState()                          —— 等待固件启动完成；
 *   3) VL53L1X_InitDB() / sensor_init               —— 写入数百字节私有配置；
 *   4) VL53L1X_SetDistanceMode() / SetTimingBudget / SetROI / SetInterMeasurementPeriod；
 *   5) VL53L1X_StartRanging()。
 *
 * 其中第 3) 步的私有配置与 timing budget 表由 ST API 提供，无法凭数据手册
 * 推导；若省略，器件以 NVM 缺省参数测距，距离分辨率与量程达不到手册指标。
 * 因此本驱动只实现 1)、2)、5) 的基础路径。
 * 需按 datasheet / ST API 补充：私有初始化块、0x005E/0x0060 时序配置、
 * 0x004B~0x004F 相位校准、ROI(0x007F~0x0082)、距离模式(0x0009)。
 */

static int vl53l1x_write_u8(vl53l1x_handle_t *handle, uint16_t reg, uint8_t value)
{
    return driver_i2c_write_reg16(&handle->i2c, reg, &value, 1U);
}

static int vl53l1x_read_u8(vl53l1x_handle_t *handle, uint16_t reg, uint8_t *value)
{
    return driver_i2c_read_reg16(&handle->i2c, reg, value, 1U);
}

/* 软复位：写 0 再写 1，中间各留复位时间 */
static int vl53l1x_soft_reset(vl53l1x_handle_t *handle)
{
    int ret;

    ret = vl53l1x_write_u8(handle, VL53L1X_REG_SOFT_RESET, VL53L1X_RESET_OFF);
    if (ret != DRIVER_OK) {
        LOG_W("vl53l1x", "assert reset failed: %d", ret);
    }
    driver_delay_ms(VL53L1X_RESET_DELAY_MS);

    ret = vl53l1x_write_u8(handle, VL53L1X_REG_SOFT_RESET, VL53L1X_RESET_ON);
    if (ret != DRIVER_OK) {
        LOG_E("vl53l1x", "release reset failed: %d", ret);
        return ret;
    }
    driver_delay_ms(VL53L1X_RESET_DELAY_MS);
    return DRIVER_OK;
}

/* 等待固件启动完成（FIRMWARE__SYSTEM_STATUS bit0） */
static int vl53l1x_wait_boot(vl53l1x_handle_t *handle)
{
    uint32_t start = driver_tick_ms();
    uint8_t status = 0U;
    int ret;

    for (;;) {
        ret = vl53l1x_read_u8(handle, VL53L1X_REG_FIRMWARE__SYSTEM_STATUS, &status);
        if (ret != DRIVER_OK) {
            return ret;
        }
        if ((status & VL53L1X_FW_BOOT_MASK) != 0U) {
            return DRIVER_OK;
        }
        if ((driver_tick_ms() - start) >= VL53L1X_RANGE_TIMEOUT_MS) {
            LOG_E("vl53l1x", "firmware boot timeout, status=0x%02X", (unsigned)status);
            return (int)HAL_TIMEOUT;
        }
        driver_delay_ms(VL53L1X_POLL_INTERVAL_MS);
    }
}

/* 中断状态 bit0 == 0 表示新样本就绪 */
static int vl53l1x_wait_range_ready(vl53l1x_handle_t *handle)
{
    uint32_t start = driver_tick_ms();
    uint8_t status = 0U;
    int ret;

    for (;;) {
        ret = vl53l1x_read_u8(handle, VL53L1X_REG_GPIO__TIO_HV_STATUS, &status);
        if (ret != DRIVER_OK) {
            return ret;
        }
        if ((status & VL53L1X_INT_STATUS_MASK) == 0U) {
            return DRIVER_OK;
        }
        if ((driver_tick_ms() - start) >= VL53L1X_RANGE_TIMEOUT_MS) {
            LOG_W("vl53l1x", "range timeout, int status=0x%02X", (unsigned)status);
            return (int)HAL_TIMEOUT;
        }
        driver_delay_ms(VL53L1X_POLL_INTERVAL_MS);
    }
}

int vl53l1x_init(vl53l1x_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint8_t id = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : VL53L1X_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("vl53l1x", "probe failed at 0x%02X", (unsigned)handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    ret = vl53l1x_soft_reset(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    ret = vl53l1x_wait_boot(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* ID 校验：model id 必须为 0xEA */
    ret = vl53l1x_read_u8(handle, VL53L1X_REG_IDENTIFICATION__MODEL_ID, &id);
    if (ret != DRIVER_OK) {
        LOG_E("vl53l1x", "read model id failed: %d", ret);
        return ret;
    }
    if (id != VL53L1X_MODEL_ID_VALUE) {
        LOG_E("vl53l1x", "unexpected model id 0x%02X", (unsigned)id);
        return (int)HAL_NOT_FOUND;
    }

    /* 待补：私有初始化块 / timing budget / ROI / 相位校准（需按 datasheet 与 ST API 移植） */

    handle->initialized = true;
    LOG_I("vl53l1x", "init ok");
    return DRIVER_OK;
}

int vl53l1x_deinit(vl53l1x_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int vl53l1x_read(vl53l1x_handle_t *handle, vl53l1x_data_t *data)
{
    uint8_t buf[2];
    uint8_t status = 0U;
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = vl53l1x_write_u8(handle, VL53L1X_REG_SYSTEM_START, VL53L1X_SYSTEM_START_DSS);
    if (ret != DRIVER_OK) {
        LOG_E("vl53l1x", "start ranging failed: %d", ret);
        return ret;
    }

    ret = vl53l1x_wait_range_ready(handle);
    if (ret != DRIVER_OK) {
        (void)vl53l1x_write_u8(handle, VL53L1X_REG_SYSTEM_START, VL53L1X_SYSTEM_STOP);
        return ret;
    }

    ret = driver_i2c_read_reg16(&handle->i2c, VL53L1X_REG_RESULT__RANGE_MM, buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        LOG_E("vl53l1x", "read distance failed: %d", ret);
        (void)vl53l1x_write_u8(handle, VL53L1X_REG_SYSTEM_START, VL53L1X_SYSTEM_STOP);
        return ret;
    }
    data->distance_mm = (uint16_t)driver_be16(buf);

    if (vl53l1x_read_u8(handle, VL53L1X_REG_RESULT__RANGE_STATUS, &status) != DRIVER_OK) {
        status = 0xFFU;
    }
    data->range_status = (uint8_t)(status & VL53L1X_RANGE_STATUS_MASK);

    (void)vl53l1x_write_u8(handle, VL53L1X_REG_SYSTEM_INTERRUPT_CLEAR, VL53L1X_INTERRUPT_CLEAR_ALL);
    (void)vl53l1x_write_u8(handle, VL53L1X_REG_SYSTEM_START, VL53L1X_SYSTEM_STOP);

    if (data->range_status != 0U) {
        LOG_D("vl53l1x", "range status %u, dist=%umm",
              (unsigned)data->range_status, (unsigned)data->distance_mm);
    }
    return DRIVER_OK;
}

int vl53l1x_read_distance(vl53l1x_handle_t *handle, uint16_t *distance_mm)
{
    vl53l1x_data_t data;
    int ret;

    if (distance_mm == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = vl53l1x_read(handle, &data);
    if (ret != DRIVER_OK) {
        return ret;
    }

    *distance_mm = data.distance_mm;
    return (data.range_status == 0U) ? DRIVER_OK : (int)HAL_NOT_READY;
}
