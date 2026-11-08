#include "ms5611.h"
#include "ms5611_register.h"
#include "log.h"

/* 各过采样档位的最大转换时间（ms）：0.6 / 1.17 / 2.28 / 4.54 / 9.04 */
static const uint32_t ms5611_conv_delay_ms[MS5611_OSR_COUNT] = { 1U, 2U, 3U, 5U, 10U };

static int ms5611_command(ms5611_handle_t *handle, uint8_t cmd)
{
    if (handle->use_spi) {
        return driver_spi_write_cs(&handle->spi, &cmd, 1U);
    }
    return driver_i2c_write(&handle->i2c, &cmd, 1U);
}

/* 发送命令后在同一总线事务中读回 len 字节（PROM 与 ADC 读取共用） */
static int ms5611_read_after_cmd(ms5611_handle_t *handle, uint8_t cmd,
                                 uint8_t *data, size_t len)
{
    uint8_t tx[1U + MS5611_ADC_DATA_SIZE];
    uint8_t rx[1U + MS5611_ADC_DATA_SIZE];
    size_t i;
    int ret;

    if (data == NULL || len == 0U || len > MS5611_ADC_DATA_SIZE) {
        return (int)HAL_INVALID_PARAM;
    }
    if (!handle->use_spi) {
        ret = driver_i2c_write(&handle->i2c, &cmd, 1U);
        return (ret != DRIVER_OK) ? ret : driver_i2c_read(&handle->i2c, data, len);
    }

    /* SPI：CS 保持低电平，命令字节后补 0xFF 时钟读出数据 */
    tx[0] = cmd;
    for (i = 0U; i < len; i++) {
        tx[i + 1U] = 0xFFU;
    }
    driver_spi_select(&handle->spi);
    ret = driver_spi_transfer(&handle->spi, tx, rx, len + 1U);
    driver_spi_deselect(&handle->spi);
    if (ret != DRIVER_OK) {
        return ret;
    }
    for (i = 0U; i < len; i++) {
        data[i] = rx[i + 1U];
    }
    return DRIVER_OK;
}

/* PROM CRC4：word0 高 4 位为校验值，返回 0 表示通过 */
static uint8_t ms5611_crc4(const uint16_t *prom)
{
    uint16_t n_rem = 0U;
    uint16_t crc_read = (uint16_t)(prom[0] >> 12);
    uint16_t word;
    uint8_t i;
    uint8_t bit;

    for (i = 0U; i < 16U; i++) {
        /* word0 的高 4 位是 CRC 本身，参与运算前要屏蔽 */
        word = (i == 0U) ? (uint16_t)(prom[0] & 0x0FFFU) : prom[i >> 1];
        n_rem ^= ((i & 1U) != 0U) ? (uint16_t)(word & 0x00FFU) : (uint16_t)(word >> 8);
        for (bit = 8U; bit > 0U; bit--) {
            n_rem = (uint16_t)(((n_rem & 0x8000U) != 0U)
                               ? ((uint16_t)(n_rem << 1) ^ 0x3000U) : (uint16_t)(n_rem << 1));
        }
    }
    return (uint8_t)(((uint16_t)(n_rem >> 12) & 0x000FU) ^ crc_read);
}

static int ms5611_read_prom(ms5611_handle_t *handle)
{
    uint8_t d[MS5611_PROM_DATA_SIZE];
    uint8_t i;

    for (i = 0U; i < MS5611_PROM_WORDS; i++) {
        if (ms5611_read_after_cmd(handle, MS5611_CMD_PROM_READ(i), d, sizeof(d)) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
        handle->prom[i] = (uint16_t)(((uint16_t)d[0] << 8) | (uint16_t)d[1]);
    }
    return DRIVER_OK;
}

/* datasheet 二阶补偿：温度输出 0.01 ℃，压力输出 Pa */
static int ms5611_compensate(const ms5611_handle_t *handle, int32_t d1, int32_t d2,
                             int32_t *temp_centi, int32_t *press_pa)
{
    int64_t c1 = (int64_t)handle->prom[1], c2 = (int64_t)handle->prom[2];
    int64_t c3 = (int64_t)handle->prom[3], c4 = (int64_t)handle->prom[4];
    int64_t c5 = (int64_t)handle->prom[5], c6 = (int64_t)handle->prom[6];
    int64_t dt, temp, off, sens, p;

    if (c1 == 0 || c2 == 0 || c5 == 0 || c6 == 0) {   /* 校准数据无效 */
        return (int)HAL_INVALID_PARAM;
    }

    dt = (int64_t)d2 - (c5 << 8);
    temp = 2000 + (dt * c6) / 8388608LL;             /* 2^23，0.01 ℃ */
    off = (c2 << 16) + (dt * c4) / 128LL;            /* 2^7 */
    sens = (c1 << 15) + (dt * c3) / 256LL;           /* 2^8 */

    if (temp < 2000) {
        int64_t d = temp - 2000;
        int64_t t2 = (dt * dt) / 2147483648LL;       /* 2^31 */
        int64_t off2 = (5 * d * d) / 2, sens2 = (5 * d * d) / 4;

        if (temp < -1500) {
            int64_t d_low = temp + 1500;
            off2 += 7 * d_low * d_low;
            sens2 += (11 * d_low * d_low) / 2;
        }
        temp -= t2;
        off -= off2;
        sens -= sens2;
    }

    if (sens == 0) {
        return (int)HAL_ERROR;
    }
    /* P = D1*SENS/2^21 - OFF，再除 2^15 得到 Pa */
    p = (((int64_t)d1 * sens) / 2097152LL - off) / 32768LL;
    if (p <= 0) {                                    /* 非物理值 */
        return (int)HAL_ERROR;
    }

    *temp_centi = (int32_t)temp;
    *press_pa = (int32_t)p;
    return DRIVER_OK;
}

/* 一次 ADC 转换：发命令 -> 等待 -> 读 24 位结果 */
static int ms5611_convert(ms5611_handle_t *handle, uint8_t cmd, uint32_t *raw)
{
    uint8_t buf[MS5611_ADC_DATA_SIZE];
    uint32_t value;

    if (ms5611_command(handle, cmd) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_ms(ms5611_conv_delay_ms[handle->osr & 0x07U]);
    if (ms5611_read_after_cmd(handle, MS5611_CMD_ADC_READ, buf, sizeof(buf)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    value = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | (uint32_t)buf[2];
    if (value == 0U || value == 0xFFFFFFU) {          /* ADC 无有效输出 */
        return (int)HAL_ERROR;
    }
    *raw = value;
    return DRIVER_OK;
}

static void ms5611_reset_bus(ms5611_handle_t *handle, bool use_spi)
{
    handle->use_spi = use_spi;
    handle->i2c.handle = NULL;
    handle->i2c.address = MS5611_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->spi.handle = NULL;
    handle->spi.cs = 0U;
    handle->spi.timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT;
    handle->osr = MS5611_OSR_DEFAULT;
    handle->initialized = false;
}

static int ms5611_setup(ms5611_handle_t *handle)
{
    uint8_t i;

    if (ms5611_command(handle, MS5611_CMD_RESET) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_ms(MS5611_RESET_DELAY_MS);

    if (ms5611_read_prom(handle) != DRIVER_OK) {
        LOG_E("ms5611", "read prom failed");
        return (int)HAL_IO_ERROR;
    }
    if (ms5611_crc4(handle->prom) != 0U) {
        LOG_E("ms5611", "prom crc mismatch (not a ms5611/ms5607?)");
        return (int)HAL_NOT_FOUND;
    }
    for (i = 1U; i <= 6U; i++) {
        if (handle->prom[i] == 0U || handle->prom[i] == 0xFFFFU) {
            LOG_E("ms5611", "invalid prom word %u: 0x%04X",
                  (unsigned)i, (unsigned)handle->prom[i]);
            return (int)HAL_INVALID_PARAM;
        }
    }

    handle->initialized = true;
    LOG_I("ms5611", "init ok (%s)", handle->use_spi ? "spi" : "i2c");
    return DRIVER_OK;
}

int ms5611_init(ms5611_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ms5611_reset_bus(handle, false);
    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : MS5611_DEFAULT_ADDRESS;
    return ms5611_setup(handle);
}

int ms5611_init_spi(ms5611_handle_t *handle, hal_spi_t *spi, hal_pin_t cs)
{
    if (handle == NULL || spi == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ms5611_reset_bus(handle, true);
    handle->spi.handle = spi;
    handle->spi.cs = cs;
    return ms5611_setup(handle);
}

int ms5611_deinit(ms5611_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    handle->spi.handle = NULL;
    return DRIVER_OK;
}

int ms5611_get_prom(const ms5611_handle_t *handle, uint16_t *prom, size_t count)
{
    size_t i;

    if (handle == NULL || prom == NULL || count == 0U || count > MS5611_PROM_WORDS ||
        !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    for (i = 0U; i < count; i++) {
        prom[i] = handle->prom[i];
    }
    return DRIVER_OK;
}

int ms5611_read_raw(ms5611_handle_t *handle, int32_t *d1, int32_t *d2)
{
    uint32_t raw_p = 0U;
    uint32_t raw_t = 0U;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (d1 == NULL && d2 == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = ms5611_convert(handle, MS5611_CMD_CONV_D1(handle->osr), &raw_p);
    if (ret == DRIVER_OK) {
        ret = ms5611_convert(handle, MS5611_CMD_CONV_D2(handle->osr), &raw_t);
    }
    if (ret != DRIVER_OK) {
        return ret;
    }

    if (d1 != NULL) { *d1 = (int32_t)raw_p; }      /* 压力 ADC */
    if (d2 != NULL) { *d2 = (int32_t)raw_t; }      /* 温度 ADC */
    return DRIVER_OK;
}

int ms5611_read(ms5611_handle_t *handle, ms5611_data_t *data)
{
    int32_t d1 = 0, d2 = 0;
    int32_t temp_centi = 0, press_pa = 0;
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = ms5611_read_raw(handle, &d1, &d2);
    if (ret != DRIVER_OK) {
        return ret;
    }
    ret = ms5611_compensate(handle, d1, d2, &temp_centi, &press_pa);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if ((float)press_pa < MS5611_PRESSURE_MIN_PA ||
        (float)press_pa > MS5611_PRESSURE_MAX_PA) {
        LOG_W("ms5611", "pressure out of range: %ld Pa", (long)press_pa);
        return (int)HAL_ERROR;
    }

    data->temperature = (float)temp_centi / 100.0f;
    data->pressure = (float)press_pa;
    return DRIVER_OK;
}
