#include "am2320.h"
#include "am2320_register.h"
#include "log.h"

/*
 * 唤醒器件：AM2320 平时处于休眠，只有收到一次寻址后才响应。
 * 这里用一次 0 长度写（仅地址 + STOP）实现唤醒，不发送任何数据字节，
 * 避免被器件当作非法功能码。部分 port 实现不支持 0 长度传输，
 * 因此失败时不视为致命错误，仅提示。
 */
static void am2320_wakeup(am2320_handle_t *handle)
{
    uint8_t dummy = 0x00U;

    if (driver_i2c_write(&handle->i2c, &dummy, 0U) != DRIVER_OK) {
        LOG_D("am2320", "wakeup write ignored");
    }
    driver_delay_ms(AM2320_WAKE_DELAY_MS);
}

int am2320_read_raw(am2320_handle_t *handle, uint8_t frame[8])
{
    uint8_t command[3];
    uint16_t crc;
    int ret;

    if (handle == NULL || frame == NULL || handle->i2c.handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    am2320_wakeup(handle);

    command[0] = AM2320_FUNC_READ;
    command[1] = (uint8_t)(AM2320_REG_START >> 8);
    command[2] = (uint8_t)AM2320_REG_COUNT;
    ret = driver_i2c_write(&handle->i2c, command, sizeof(command));
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 器件需要 >= 1.5ms 准备数据，等待不足会读到 NACK 或旧帧 */
    driver_delay_ms(AM2320_READ_DELAY_MS);

    ret = driver_i2c_read(&handle->i2c, frame, AM2320_FRAME_LEN);
    if (ret != DRIVER_OK) {
        return ret;
    }

    if (frame[0] != AM2320_FUNC_READ || frame[1] != AM2320_REG_COUNT) {
        LOG_E("am2320", "bad frame head 0x%02X 0x%02X", frame[0], frame[1]);
        return (int)IOT_IO_ERROR;
    }

    /* CRC16/Modbus 覆盖前 6 字节，低字节先发 */
    crc = driver_crc16_modbus(frame, AM2320_FRAME_LEN - 2U);
    if ((uint8_t)(crc & 0xFFU) != frame[6] || (uint8_t)(crc >> 8) != frame[7]) {
        LOG_E("am2320", "crc error");
        return (int)IOT_CRC_ERROR;
    }

    return DRIVER_OK;
}

int am2320_init(am2320_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t frame[AM2320_FRAME_LEN];

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : AM2320_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;

    /* 器件休眠时不应答 probe，先唤醒再探测 */
    am2320_wakeup(handle);
    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("am2320", "probe failed at 0x%02X", handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    handle->initialized = true;

    /* 试读一帧，确认帧头与 CRC 正常（同时完成一次有效性校验） */
    if (am2320_read_raw(handle, frame) != DRIVER_OK) {
        handle->initialized = false;
        LOG_E("am2320", "first read failed");
        return (int)IOT_IO_ERROR;
    }

    LOG_I("am2320", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int am2320_deinit(am2320_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int am2320_read(am2320_handle_t *handle, float *temperature, float *humidity)
{
    uint8_t frame[AM2320_FRAME_LEN];
    uint16_t raw_h;
    uint16_t raw_t;
    float t;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (temperature == NULL && humidity == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = am2320_read_raw(handle, frame);
    if (ret != DRIVER_OK) {
        return ret;
    }

    raw_h = (uint16_t)(((uint16_t)frame[2] << 8) | (uint16_t)frame[3]);
    raw_t = (uint16_t)(((uint16_t)frame[4] << 8) | (uint16_t)frame[5]);

    if (temperature != NULL) {
        t = (float)(raw_t & AM2320_TEMP_MASK) / AM2320_SCALE;
        if ((raw_t & AM2320_TEMP_SIGN) != 0U) {
            t = -t;   /* bit15 为符号位，负温度取反 */
        }
        *temperature = t;
    }
    if (humidity != NULL) {
        *humidity = (float)raw_h / AM2320_SCALE;
    }

    return DRIVER_OK;
}
