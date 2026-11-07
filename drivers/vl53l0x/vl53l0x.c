#include "vl53l0x.h"
#include "vl53l0x_register.h"
#include "log.h"

/*
 * VL53L0X 初始化说明（重要）：
 *
 * ST 官方 API 的完整上电流程为：
 *   1) VL53L0X_DataInit()            —— 软复位 + 读 NVM 默认参数；
 *   2) VL53L0X_StaticInit()          —— 写入数百字节 TuningSettings、
 *                                      SPAD 管理与参考校准（含 0x2C~0x8E 私有页）；
 *   3) VL53L0X_PerformRefCalibration —— VHV / 相位校准；
 *   4) VL53L0X_SetDeviceMode(SINGLE_RANGING) + 时序预算配置。
 *
 * 其中 TuningSettings 私有寄存器序列随固件版本变化，无法凭数据手册推导，
 * 因此本文件只实现 1) 与 4) 的基础测距路径：
 *   - 缺省 NVM 参数下器件仍能完成一次测量，但量程/线性度/环境光抑制
 *     达不到手册指标，range_status 可能常为非 0；
 *   - 需按 datasheet / ST API 补充（待补：TuningSettings、SPAD、参考校准、
 *     measurement timing budget 0x71/0x72）。补充前请以上层 range_status
 *     判定数据是否可用。
 */

static int vl53l0x_write_u8(vl53l0x_handle_t *handle, uint8_t reg, uint8_t value)
{
    return driver_i2c_write_reg(&handle->i2c, reg, &value, 1U);
}

static int vl53l0x_read_u8(vl53l0x_handle_t *handle, uint8_t reg, uint8_t *value)
{
    return driver_i2c_read_reg(&handle->i2c, reg, value, 1U);
}

/* 轮询 RESULT_INTERRUPT_STATUS 的"新样本就绪"位 */
static int vl53l0x_wait_range_ready(vl53l0x_handle_t *handle)
{
    uint32_t start = driver_tick_ms();
    uint8_t status = 0U;
    int ret;

    for (;;) {
        ret = vl53l0x_read_u8(handle, VL53L0X_REG_RESULT_INTERRUPT_STATUS, &status);
        if (ret != DRIVER_OK) {
            return ret;
        }
        if ((status & VL53L0X_INTERRUPT_NEW_SAMPLE_MASK) != 0U) {
            return DRIVER_OK;
        }
        if ((driver_tick_ms() - start) >= VL53L0X_RANGE_TIMEOUT_MS) {
            LOG_W("vl53l0x", "range timeout, int status=0x%02X", (unsigned)status);
            return (int)IOT_TIMEOUT;
        }
        driver_delay_ms(VL53L0X_POLL_INTERVAL_MS);
    }
}

/* 软复位：先拉低再释放 SOFTRESET_N */
static int vl53l0x_soft_reset(vl53l0x_handle_t *handle)
{
    int ret;

    ret = vl53l0x_write_u8(handle, VL53L0X_REG_SOFT_RESET, 0x00U);
    if (ret != DRIVER_OK) {
        /* 冷启动时器件可能已处于复位态，不视为致命错误 */
        LOG_W("vl53l0x", "assert reset failed: %d", ret);
    }
    driver_delay_ms(VL53L0X_RESET_DELAY_MS);

    ret = vl53l0x_write_u8(handle, VL53L0X_REG_SOFT_RESET, 0x01U);
    if (ret != DRIVER_OK) {
        LOG_E("vl53l0x", "release reset failed: %d", ret);
        return ret;
    }
    driver_delay_ms(VL53L0X_RESET_DELAY_MS);
    return DRIVER_OK;
}

int vl53l0x_init(vl53l0x_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t id = 0U;
    uint8_t revision = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : VL53L0X_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->revision = 0U;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("vl53l0x", "probe failed at 0x%02X", (unsigned)handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    ret = vl53l0x_soft_reset(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* ID 校验：model id 必须为 0xEE */
    ret = vl53l0x_read_u8(handle, VL53L0X_REG_MODEL_ID, &id);
    if (ret != DRIVER_OK) {
        LOG_E("vl53l0x", "read model id failed: %d", ret);
        return ret;
    }
    if (id != VL53L0X_MODEL_ID_VALUE) {
        LOG_E("vl53l0x", "unexpected model id 0x%02X", (unsigned)id);
        return (int)IOT_NOT_FOUND;
    }

    if (vl53l0x_read_u8(handle, VL53L0X_REG_REVISION_ID, &revision) == DRIVER_OK) {
        handle->revision = revision;
    }

    /* 待补：TuningSettings / SPAD 管理 / 参考校准（需按 datasheet 与 ST API 移植） */

    handle->initialized = true;
    LOG_I("vl53l0x", "init ok, rev=0x%02X", (unsigned)handle->revision);
    return DRIVER_OK;
}

int vl53l0x_deinit(vl53l0x_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int vl53l0x_read(vl53l0x_handle_t *handle, vl53l0x_data_t *data)
{
    uint8_t block[VL53L0X_RESULT_BLOCK_LEN];
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = vl53l0x_write_u8(handle, VL53L0X_REG_SYSRANGE_START,
                           VL53L0X_SYSRANGE_START_SINGLE);
    if (ret != DRIVER_OK) {
        LOG_E("vl53l0x", "start single shot failed: %d", ret);
        return ret;
    }

    ret = vl53l0x_wait_range_ready(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    ret = driver_i2c_read_reg(&handle->i2c, VL53L0X_REG_RESULT_RANGE_STATUS,
                              block, sizeof(block));
    if (ret != DRIVER_OK) {
        LOG_E("vl53l0x", "read result block failed: %d", ret);
        return ret;
    }

    /* 清中断，允许下一次测量 */
    (void)vl53l0x_write_u8(handle, VL53L0X_REG_SYSTEM_INTERRUPT_CLEAR,
                           VL53L0X_INTERRUPT_CLEAR_ALL);

    data->range_status = (uint8_t)((block[0] & VL53L0X_RANGE_STATUS_MASK)
                                   >> VL53L0X_RANGE_STATUS_SHIFT);
    data->distance_mm = (uint16_t)driver_be16(&block[VL53L0X_RESULT_RANGE_OFFSET]);

    if (data->range_status != 0U) {
        LOG_D("vl53l0x", "range status %u, dist=%umm",
              (unsigned)data->range_status, (unsigned)data->distance_mm);
    }
    return DRIVER_OK;
}

int vl53l0x_read_distance(vl53l0x_handle_t *handle, uint16_t *distance_mm)
{
    vl53l0x_data_t data;
    int ret;

    if (distance_mm == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = vl53l0x_read(handle, &data);
    if (ret != DRIVER_OK) {
        return ret;
    }

    *distance_mm = data.distance_mm;
    return (data.range_status == 0U) ? DRIVER_OK : (int)IOT_NOT_READY;
}
