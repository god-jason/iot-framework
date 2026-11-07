#include "max31865.h"
#include "max31865_register.h"
#include "log.h"

static int max31865_write_reg(max31865_handle_t *handle, uint8_t reg, uint8_t value)
{
    uint8_t tx[2];
    uint8_t rx[2];
    int ret;

    tx[0] = (uint8_t)(MAX31865_SPI_WRITE | (reg & 0x7FU));
    tx[1] = value;

    driver_spi_select(&handle->spi);
    ret = driver_spi_transfer(&handle->spi, tx, rx, sizeof(tx));
    driver_spi_deselect(&handle->spi);
    if (ret != DRIVER_OK) {
        LOG_E("max31865", "write reg 0x%02X failed: %d", (unsigned int)reg, ret);
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}

static int max31865_read_regs(max31865_handle_t *handle, uint8_t reg,
                              uint8_t *data, size_t len)
{
    uint8_t tx[5];
    uint8_t rx[5];
    size_t i;
    int ret;

    if (len == 0U || len > 4U) {
        return (int)IOT_INVALID_PARAM;
    }

    tx[0] = (uint8_t)(MAX31865_SPI_READ | (reg & 0x7FU));
    for (i = 0U; i < len; i++) {
        tx[i + 1U] = 0x00U;
    }

    driver_spi_select(&handle->spi);
    ret = driver_spi_transfer(&handle->spi, tx, rx, len + 1U);
    driver_spi_deselect(&handle->spi);
    if (ret != DRIVER_OK) {
        LOG_E("max31865", "read reg 0x%02X failed: %d", (unsigned int)reg, ret);
        return (int)IOT_IO_ERROR;
    }

    for (i = 0U; i < len; i++) {
        data[i] = rx[i + 1U];
    }
    return DRIVER_OK;
}

static uint8_t max31865_config_value(const max31865_handle_t *handle)
{
    uint8_t config = (uint8_t)(MAX31865_CONFIG_VBIAS | MAX31865_CONFIG_MODE_AUTO |
                               MAX31865_CONFIG_FILTER_50HZ);

    if (handle->wire == 3U) {
        config = (uint8_t)(config | MAX31865_CONFIG_3WIRE);
    }
    return config;
}

static int max31865_apply_config(max31865_handle_t *handle)
{
    int ret = max31865_write_reg(handle, MAX31865_REG_CONFIG, max31865_config_value(handle));

    if (ret != DRIVER_OK) {
        return ret;
    }
    /* 自动模式下首次转换需要约 1 个滤波周期（50Hz 约 66ms），保守等待 100ms */
    driver_delay_ms(MAX31865_CONV_AUTO_MS);
    return DRIVER_OK;
}

/* 牛顿迭代开方：避免引入 libm 依赖，20 次迭代足够收敛到 float 精度 */
static float max31865_sqrt(float value)
{
    float guess;
    uint8_t i;

    if (value <= 0.0f) {
        return 0.0f;
    }
    guess = (value > 1.0f) ? value : 1.0f;
    for (i = 0U; i < 20U; i++) {
        guess = 0.5f * (guess + value / guess);
    }
    return guess;
}

/* Callendar-Van Dusen：电阻 -> 温度（℃） */
static float max31865_resistance_to_celsius(float resistance, float r0)
{
    const float a = MAX31865_CVD_A;
    const float b = MAX31865_CVD_B;
    const float c = MAX31865_CVD_C;
    float ratio = resistance / r0;
    float t;

    if (ratio >= 1.0f) {
        /* T >= 0：C 项为 0，直接解二次方程 */
        float disc = a * a - 4.0f * b * (1.0f - ratio);

        return (-a + max31865_sqrt(disc)) / (2.0f * b);
    }

    /* T < 0：含三次项，用牛顿迭代求根（初值取线性近似） */
    t = (ratio - 1.0f) / a;
    {
        uint8_t i;

        for (i = 0U; i < 4U; i++) {
            float t2 = t * t;
            float t3 = t2 * t;
            float f = 1.0f + a * t + b * t2 + c * (t - 100.0f) * t3 - ratio;
            float df = a + 2.0f * b * t + c * (4.0f * t3 - 300.0f * t2);

            if (df == 0.0f) {
                break;
            }
            t -= f / df;
        }
    }
    return t;
}

int max31865_init(max31865_handle_t *handle, iot_spi_t *spi, iot_pin_t cs)
{
    uint8_t config = 0U;

    if (handle == NULL || spi == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->spi.handle = spi;
    handle->spi.cs = cs;
    handle->spi.timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT;
    handle->rref = MAX31865_DEFAULT_RREF;
    handle->r0 = MAX31865_DEFAULT_R0;
    handle->wire = 2U;
    handle->initialized = false;

    driver_delay_ms(MAX31865_POWER_ON_MS);

    /* 读配置寄存器校验 SPI 链路；无器件时 MISO 多为 0xFF */
    if (max31865_read_regs(handle, MAX31865_REG_CONFIG, &config, 1U) != DRIVER_OK ||
        config == 0xFFU) {
        LOG_E("max31865", "probe failed");
        return (int)IOT_NOT_FOUND;
    }

    handle->initialized = true;
    if (max31865_apply_config(handle) != DRIVER_OK) {
        handle->initialized = false;
        return (int)IOT_IO_ERROR;
    }

    LOG_I("max31865", "init ok, config=0x%02X", (unsigned int)config);
    return DRIVER_OK;
}

int max31865_deinit(max31865_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)IOT_NOT_READY;
    }

    /* 关闭 VBIAS 以减少 RTD 自热 */
    (void)max31865_write_reg(handle, MAX31865_REG_CONFIG, MAX31865_CONFIG_FAULT_CLEAR);
    if (handle->spi.cs != 0U) {
        driver_spi_deselect(&handle->spi);
    }
    handle->initialized = false;
    handle->spi.handle = NULL;
    return DRIVER_OK;
}

int max31865_set_wire_mode(max31865_handle_t *handle, uint8_t wires)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (wires != 2U && wires != 3U && wires != 4U) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->wire = wires;
    return max31865_apply_config(handle);
}

int max31865_set_rref(max31865_handle_t *handle, float rref)
{
    if (handle == NULL || !handle->initialized || rref <= 0.0f) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->rref = rref;
    return DRIVER_OK;
}

int max31865_set_r0(max31865_handle_t *handle, float r0)
{
    if (handle == NULL || !handle->initialized || r0 <= 0.0f) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->r0 = r0;
    return DRIVER_OK;
}

int max31865_read_raw(max31865_handle_t *handle, uint16_t *raw)
{
    uint8_t buf[2];

    if (handle == NULL || raw == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    if (max31865_read_regs(handle, MAX31865_REG_RTD_MSB, buf, sizeof(buf)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    {
        uint16_t value = (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);

        /* LSB 的 bit0 为故障标志，右移 1 位得到 15 位 ADC 码 */
        if ((value & MAX31865_RTD_FAULT_BIT) != 0U) {
            LOG_E("max31865", "rtd fault bit set");
            return (int)IOT_HW_ERROR;
        }
        *raw = (uint16_t)(value >> 1);
    }
    return DRIVER_OK;
}

int max31865_read_resistance(max31865_handle_t *handle, float *ohms)
{
    uint16_t raw = 0U;
    int ret;

    if (ohms == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = max31865_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *ohms = (float)raw * handle->rref / MAX31865_RTD_FULL_SCALE;
    return DRIVER_OK;
}

int max31865_read(max31865_handle_t *handle, float *celsius)
{
    float resistance = 0.0f;
    int ret;

    if (celsius == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = max31865_read_resistance(handle, &resistance);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *celsius = max31865_resistance_to_celsius(resistance, handle->r0);
    return DRIVER_OK;
}

int max31865_get_fault(max31865_handle_t *handle, uint8_t *fault)
{
    uint8_t status = 0U;

    if (handle == NULL || fault == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    if (max31865_read_regs(handle, MAX31865_REG_FAULT_STATUS, &status, 1U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    *fault = status;
    return DRIVER_OK;
}

int max31865_clear_fault(max31865_handle_t *handle)
{
    uint8_t config;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    /* FAULT_CLEAR 位自清零，写入后恢复正常配置 */
    config = (uint8_t)(max31865_config_value(handle) | MAX31865_CONFIG_FAULT_CLEAR);
    return max31865_write_reg(handle, MAX31865_REG_CONFIG, config);
}
