#include "ms5837.h"
#include "ms5837_register.h"
#include "log.h"

/* 各过采样档位的最大转换时间（ms）：0.56 / 0.94 / 1.7 / 3.2 / 6.1 / 11.7 */
static const uint32_t ms5837_conv_delay_ms[MS5837_OSR_COUNT] = { 1U, 2U, 3U, 4U, 7U, 12U };

static int ms5837_command(ms5837_handle_t *handle, uint8_t cmd)
{
    return driver_i2c_write(&handle->i2c, &cmd, 1U);
}

/* 发送命令后读回 len 字节（PROM / ADC 共用） */
static int ms5837_read_after_cmd(ms5837_handle_t *handle, uint8_t cmd,
                                 uint8_t *data, size_t len)
{
    int ret;

    if (data == NULL || len == 0U || len > MS5837_ADC_DATA_SIZE) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = driver_i2c_write(&handle->i2c, &cmd, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    return driver_i2c_read(&handle->i2c, data, len);
}

/* PROM CRC4：word0 高 4 位为校验值，返回 0 表示通过 */
static uint8_t ms5837_crc4(const uint16_t *prom)
{
    uint16_t n_rem = 0U;
    uint16_t crc_read = (uint16_t)(prom[0] >> 12);
    uint16_t word;
    uint8_t i;
    uint8_t bit;

    for (i = 0U; i < 16U; i++) {
        word = (i == 0U) ? (uint16_t)(prom[0] & 0x0FFFU) : prom[i >> 1];
        if ((i & 1U) != 0U) {
            n_rem ^= (uint16_t)(word & 0x00FFU);
        } else {
            n_rem ^= (uint16_t)(word >> 8);
        }
        for (bit = 8U; bit > 0U; bit--) {
            if ((n_rem & 0x8000U) != 0U) {
                n_rem = (uint16_t)((uint16_t)(n_rem << 1) ^ 0x3000U);
            } else {
                n_rem = (uint16_t)(n_rem << 1);
            }
        }
    }
    n_rem = (uint16_t)((n_rem >> 12) & 0x000FU);
    return (uint8_t)(n_rem ^ crc_read);
}

static int ms5837_read_prom(ms5837_handle_t *handle)
{
    uint8_t d[MS5837_PROM_DATA_SIZE];
    uint8_t i;

    for (i = 0U; i < MS5837_PROM_WORDS; i++) {
        if (ms5837_read_after_cmd(handle, MS5837_CMD_PROM_READ(i), d, sizeof(d)) != DRIVER_OK) {
            return (int)IOT_IO_ERROR;
        }
        handle->prom[i] = (uint16_t)(((uint16_t)d[0] << 8) | (uint16_t)d[1]);
    }
    return DRIVER_OK;
}

/*
 * datasheet 二阶补偿：
 *   一阶：dT = D2 - C5*2^8，TEMP = 2000 + dT*C6/2^23
 *         OFF = C2*2^17 + dT*C4/2^6，SENS = C1*2^16 + dT*C3/2^7
 *         P = (D1*SENS/2^21 - OFF)/2^13，单位 0.1 mbar
 *   二阶：低温段修正 T2/OFF2/SENS2（含 -15 ℃ 以下的附加项）
 */
static int ms5837_compensate(const ms5837_handle_t *handle, int32_t d1, int32_t d2,
                             int32_t *temp_centi, int32_t *press_dmbar)
{
    int64_t c1 = (int64_t)handle->prom[1];
    int64_t c2 = (int64_t)handle->prom[2];
    int64_t c3 = (int64_t)handle->prom[3];
    int64_t c4 = (int64_t)handle->prom[4];
    int64_t c5 = (int64_t)handle->prom[5];
    int64_t c6 = (int64_t)handle->prom[6];
    int64_t dt;
    int64_t temp;
    int64_t off;
    int64_t sens;
    int64_t p;

    if (c1 == 0 || c2 == 0 || c5 == 0 || c6 == 0) {
        return (int)IOT_INVALID_PARAM;
    }

    dt = (int64_t)d2 - (c5 << 8);
    temp = 2000 + (dt * c6) / 8388608LL;              /* 2^23，0.01 ℃ */
    off = (c2 << 17) + (dt * c4) / 64LL;              /* 2^6 */
    sens = (c1 << 16) + (dt * c3) / 128LL;            /* 2^7 */

    if (temp < 2000) {
        int64_t d = temp - 2000;
        int64_t t2 = (3 * dt * dt) / 8589934592LL;    /* 2^33 */
        int64_t off2 = (3 * d * d) / 2;
        int64_t sens2 = (5 * d * d) / 8;

        if (temp < -1500) {
            int64_t d_low = temp + 1500;
            off2 += 7 * d_low * d_low;
            sens2 += 4 * d_low * d_low;
        }
        temp -= t2;
        off -= off2;
        sens -= sens2;
    }

    if (sens == 0) {
        return (int)IOT_ERROR;
    }

    /* 结果为 0.1 mbar */
    p = (((int64_t)d1 * sens) / 2097152LL - off) / 8192LL;
    if (p <= 0) {
        return (int)IOT_ERROR;
    }

    *temp_centi = (int32_t)temp;
    *press_dmbar = (int32_t)p;
    return DRIVER_OK;
}

float ms5837_pressure_to_depth(float pressure_pa)
{
    float mbar;

    if (!(pressure_pa > 0.0f)) {
        return 0.0f;
    }
    mbar = pressure_pa / 100.0f;   /* 1 mbar = 100 Pa */
    return (mbar - MS5837_ATM_MBAR) * MS5837_MBAR_TO_METER(1.0f);
}

int ms5837_init(ms5837_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t i;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : MS5837_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->osr = MS5837_OSR_DEFAULT;
    handle->initialized = false;

    if (ms5837_command(handle, MS5837_CMD_RESET) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(MS5837_RESET_DELAY_MS);

    if (ms5837_read_prom(handle) != DRIVER_OK) {
        LOG_E("ms5837", "read prom failed");
        return (int)IOT_IO_ERROR;
    }
    if (ms5837_crc4(handle->prom) != 0U) {
        LOG_E("ms5837", "prom crc mismatch (not a ms5837?)");
        return (int)IOT_NOT_FOUND;
    }
    for (i = 1U; i <= 6U; i++) {
        if (handle->prom[i] == 0U || handle->prom[i] == 0xFFFFU) {
            LOG_E("ms5837", "invalid prom word %u: 0x%04X",
                  (unsigned)i, (unsigned)handle->prom[i]);
            return (int)IOT_INVALID_PARAM;
        }
    }

    handle->initialized = true;
    LOG_I("ms5837", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int ms5837_deinit(ms5837_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int ms5837_get_prom(const ms5837_handle_t *handle, uint16_t *prom, size_t count)
{
    size_t i;

    if (handle == NULL || prom == NULL || count == 0U || count > MS5837_PROM_WORDS) {
        return (int)IOT_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    for (i = 0U; i < count; i++) {
        prom[i] = handle->prom[i];
    }
    return DRIVER_OK;
}

int ms5837_read_raw(ms5837_handle_t *handle, int32_t *d1, int32_t *d2)
{
    uint8_t buf[MS5837_ADC_DATA_SIZE];
    uint32_t raw_p;
    uint32_t raw_t;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (d1 == NULL && d2 == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    /* D1：压力 */
    if (ms5837_command(handle, MS5837_CMD_CONV_D1(handle->osr)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(ms5837_conv_delay_ms[handle->osr]);
    if (ms5837_read_after_cmd(handle, MS5837_CMD_ADC_READ, buf, sizeof(buf)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    raw_p = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | (uint32_t)buf[2];
    if (raw_p == 0U || raw_p == 0xFFFFFFU) {
        return (int)IOT_ERROR;
    }

    /* D2：温度 */
    if (ms5837_command(handle, MS5837_CMD_CONV_D2(handle->osr)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(ms5837_conv_delay_ms[handle->osr]);
    if (ms5837_read_after_cmd(handle, MS5837_CMD_ADC_READ, buf, sizeof(buf)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    raw_t = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | (uint32_t)buf[2];
    if (raw_t == 0U || raw_t == 0xFFFFFFU) {
        return (int)IOT_ERROR;
    }

    if (d1 != NULL) {
        *d1 = (int32_t)raw_p;
    }
    if (d2 != NULL) {
        *d2 = (int32_t)raw_t;
    }
    return DRIVER_OK;
}

int ms5837_read(ms5837_handle_t *handle, ms5837_data_t *data)
{
    int32_t d1 = 0;
    int32_t d2 = 0;
    int32_t temp_centi = 0;
    int32_t press_dmbar = 0;
    float pressure;
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = ms5837_read_raw(handle, &d1, &d2);
    if (ret != DRIVER_OK) {
        return ret;
    }

    ret = ms5837_compensate(handle, d1, d2, &temp_centi, &press_dmbar);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 0.1 mbar -> Pa */
    pressure = (float)press_dmbar * 10.0f;
    if (!(pressure >= MS5837_PRESSURE_MIN_PA && pressure <= MS5837_PRESSURE_MAX_PA)) {
        LOG_W("ms5837", "pressure out of range: %ld Pa", (long)pressure);
        return (int)IOT_ERROR;
    }

    data->temperature = (float)temp_centi / 100.0f;
    data->pressure = pressure;
    data->depth_m = ms5837_pressure_to_depth(pressure);
    return DRIVER_OK;
}
