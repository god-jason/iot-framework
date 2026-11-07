#include "mlx90614.h"
#include "mlx90614_register.h"
#include "log.h"

#define MLX90614_TAG "mlx90614"
#define MLX90614_PEC_LEN 3U /* lsb + msb + pec */

static int mlx90614_check(const mlx90614_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return DRIVER_OK;
}

static float mlx90614_to_celsius(uint16_t raw)
{
    /* 数据 bit15 为错误标志，换算前屏蔽 */
    return ((float)(raw & 0x7FFFU) * MLX90614_TEMP_LSB_C) - MLX90614_KELVIN_OFFSET;
}

int mlx90614_read_word(mlx90614_handle_t *handle, uint8_t cmd, uint16_t *value)
{
    uint8_t buf[MLX90614_PEC_LEN];
    uint8_t crc_src[4];
    int ret;

    if (handle == NULL || handle->i2c.handle == NULL || value == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    /* SMBus Read Word：写命令后重复起始读 3 字节（LSB、MSB、PEC） */
    ret = driver_i2c_read_reg(&handle->i2c, cmd, buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        return ret;
    }

    crc_src[0] = (uint8_t)(handle->i2c.address << 1);
    crc_src[1] = cmd;
    crc_src[2] = buf[0];
    crc_src[3] = buf[1];
    if (driver_crc8(crc_src, sizeof(crc_src),
                    MLX90614_CRC_POLY, MLX90614_CRC_INIT) != buf[2]) {
        LOG_E(MLX90614_TAG, "pec error at cmd 0x%02X", cmd);
        return (int)IOT_CRC_ERROR;
    }

    *value = (uint16_t)(((uint16_t)buf[1] << 8) | buf[0]);
    return DRIVER_OK;
}

int mlx90614_read_raw(mlx90614_handle_t *handle, uint8_t cmd, uint16_t *raw)
{
    int ret = mlx90614_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    return mlx90614_read_word(handle, cmd, raw);
}

int mlx90614_init(mlx90614_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint16_t config = 0U;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : MLX90614_ADDRESS_DEFAULT;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->emissivity = 1.0f;
    handle->initialized = false;

    if (handle->i2c.address < MLX90614_ADDRESS_MIN ||
        handle->i2c.address > MLX90614_ADDRESS_MAX) {
        return (int)IOT_INVALID_PARAM;
    }
    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E(MLX90614_TAG, "probe failed at 0x%02X", handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    driver_delay_ms(MLX90614_POWER_ON_MS);
    handle->initialized = true;

    /* 读配置寄存器确认总线可用；失败不阻断（部分批次不支持该命令） */
    if (mlx90614_read_word(handle, MLX90614_CMD_CONFIG, &config) == DRIVER_OK) {
        LOG_I(MLX90614_TAG, "init ok at 0x%02X, config 0x%04X",
              handle->i2c.address, (unsigned)config);
    } else {
        LOG_W(MLX90614_TAG, "init ok at 0x%02X, config read failed", handle->i2c.address);
    }

    (void)mlx90614_get_emissivity(handle, &handle->emissivity);
    return DRIVER_OK;
}

int mlx90614_deinit(mlx90614_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int mlx90614_read(mlx90614_handle_t *handle, mlx90614_data_t *data)
{
    uint16_t ambient = 0U;
    uint16_t object = 0U;
    int ret;

    if (data == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = mlx90614_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    ret = mlx90614_read_word(handle, MLX90614_CMD_TA, &ambient);
    if (ret != DRIVER_OK) {
        return ret;
    }
    ret = mlx90614_read_word(handle, MLX90614_CMD_TOBJ1, &object);
    if (ret != DRIVER_OK) {
        return ret;
    }

    data->ambient_c = mlx90614_to_celsius(ambient);
    data->object_c = mlx90614_to_celsius(object);
    return DRIVER_OK;
}

int mlx90614_write_eeprom(mlx90614_handle_t *handle, uint8_t cmd, uint16_t value)
{
    uint8_t buf[3];
    int ret = mlx90614_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 擦除：写 0x0000，等待 EEPROM 周期 */
    buf[0] = cmd;
    buf[1] = 0x00U;
    buf[2] = 0x00U;
    ret = driver_i2c_write(&handle->i2c, buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(MLX90614_EEPROM_ERASE_MS);

    /* 写值（PEC 可选；这里不发送，由器件内部处理） */
    buf[1] = (uint8_t)(value & 0xFFU);
    buf[2] = (uint8_t)(value >> 8);
    ret = driver_i2c_write(&handle->i2c, buf, sizeof(buf));
    driver_delay_ms(MLX90614_EEPROM_WRITE_MS);
    return ret;
}

int mlx90614_set_emissivity(mlx90614_handle_t *handle, float emissivity)
{
    uint16_t raw;
    int ret = mlx90614_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (emissivity < 0.1f || emissivity > 1.0f) {
        return (int)IOT_INVALID_PARAM;
    }

    raw = (uint16_t)((emissivity * 65535.0f) + 0.5f);
    ret = mlx90614_write_eeprom(handle, MLX90614_CMD_EMISSIVITY, raw);
    if (ret == DRIVER_OK) {
        handle->emissivity = emissivity;
    }
    return ret;
}

int mlx90614_get_emissivity(mlx90614_handle_t *handle, float *emissivity)
{
    uint16_t raw = 0U;
    int ret = mlx90614_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (emissivity == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = mlx90614_read_word(handle, MLX90614_CMD_EMISSIVITY, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *emissivity = (float)raw / 65535.0f;
    handle->emissivity = *emissivity;
    return DRIVER_OK;
}

int mlx90614_read_flags(mlx90614_handle_t *handle, uint16_t *flags)
{
    int ret = mlx90614_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (flags == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    return mlx90614_read_word(handle, MLX90614_CMD_FLAGS, flags);
}
