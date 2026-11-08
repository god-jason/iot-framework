#include "ads1118.h"
#include "ads1118_register.h"
#include "log.h"

/* 各数据速率对应的单次转换时间（ms） */
static const uint16_t ads1118_conv_ms[8] = { 125U, 63U, 32U, 16U, 8U, 4U, 3U, 2U };

/* PGA 满量程（mV） */
static const uint16_t ads1118_fs_mv[6] = {
    ADS1118_FS_6V144_MV, ADS1118_FS_4V096_MV, ADS1118_FS_2V048_MV,
    ADS1118_FS_1V024_MV, ADS1118_FS_0V512_MV, ADS1118_FS_0V256_MV
};

/*
 * 一次 32 位 SPI 传输：
 *   发送：config(16 位) + NOP(16 位)
 *   接收：上一次转换结果(16 位) + 配置寄存器回读(16 位)
 */
static int ads1118_transfer(ads1118_handle_t *handle, uint16_t config,
                            uint16_t *conversion, uint16_t *cfg_readback)
{
    uint8_t tx[4];
    uint8_t rx[4] = { 0U, 0U, 0U, 0U };
    int ret;

    tx[0] = (uint8_t)(config >> 8);
    tx[1] = (uint8_t)(config & 0xFFU);
    tx[2] = 0x00U;
    tx[3] = 0x00U;

    driver_spi_select(&handle->spi);
    ret = driver_spi_transfer(&handle->spi, tx, rx, sizeof(tx));
    driver_spi_deselect(&handle->spi);
    if (ret != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    if (conversion != NULL) {
        *conversion = (uint16_t)(((uint16_t)rx[0] << 8) | (uint16_t)rx[1]);
    }
    if (cfg_readback != NULL) {
        *cfg_readback = (uint16_t)(((uint16_t)rx[2] << 8) | (uint16_t)rx[3]);
    }
    return DRIVER_OK;
}

static uint16_t ads1118_build_config(const ads1118_handle_t *handle,
                                     ads1118_mux_t mux, bool start)
{
    uint16_t config;

    config = (uint16_t)(((uint16_t)mux << ADS1118_CFG_MUX_SHIFT) & ADS1118_CFG_MUX_MASK);
    config |= (uint16_t)(((uint16_t)handle->pga << ADS1118_CFG_PGA_SHIFT) & ADS1118_CFG_PGA_MASK);
    config |= (uint16_t)(((uint16_t)handle->data_rate << ADS1118_CFG_DR_SHIFT) & ADS1118_CFG_DR_MASK);
    config |= (uint16_t)(ADS1118_CFG_MODE_SINGLE | ADS1118_CFG_RESERVED);
    if (start) {
        config |= ADS1118_CFG_SS;
    }
    return config;
}

int ads1118_init(ads1118_handle_t *handle, hal_spi_t *spi, hal_pin_t cs)
{
    uint16_t cfg = 0U;

    if (handle == NULL || spi == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->spi.handle = spi;
    handle->spi.cs = cs;
    handle->spi.timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT;
    handle->pga = ADS1118_PGA_2V048;
    handle->data_rate = ADS1118_DR_128;
    handle->initialized = false;

    if (cs != 0U && driver_gpio_output(cs, HAL_GPIO_HIGH) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    driver_delay_ms(1U);   /* 上电/复位稳定 */

    /* 回读配置寄存器：bit0 恒为 1，为 0 说明总线上没有器件 */
    if (ads1118_transfer(handle, ADS1118_CFG_DEFAULT, NULL, &cfg) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if ((cfg & ADS1118_CFG_RESERVED) == 0U) {
        LOG_E("ads1118", "config readback 0x%04X invalid", (unsigned)cfg);
        return (int)HAL_NOT_FOUND;
    }

    handle->initialized = true;
    LOG_I("ads1118", "init ok, cfg=0x%04X", (unsigned)cfg);
    return DRIVER_OK;
}

int ads1118_deinit(ads1118_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    driver_spi_deselect(&handle->spi);
    handle->initialized = false;
    handle->spi.handle = NULL;
    return DRIVER_OK;
}

int ads1118_set_pga(ads1118_handle_t *handle, ads1118_pga_t pga)
{
    if (handle == NULL || !handle->initialized ||
        (uint16_t)pga > (uint16_t)ADS1118_PGA_0V256) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->pga = pga;
    return DRIVER_OK;
}

int ads1118_set_data_rate(ads1118_handle_t *handle, ads1118_dr_t data_rate)
{
    if (handle == NULL || !handle->initialized ||
        (uint16_t)data_rate > (uint16_t)ADS1118_DR_860) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->data_rate = data_rate;
    return DRIVER_OK;
}

/* 启动一次单次转换并取回本次结果 */
static int ads1118_convert_once(ads1118_handle_t *handle, uint16_t start_cfg,
                                uint16_t idle_cfg, int16_t *raw)
{
    uint16_t value = 0U;

    /* 第一次传输启动转换，同时读出的上一次结果丢弃 */
    if (ads1118_transfer(handle, start_cfg, NULL, NULL) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_ms((uint32_t)ads1118_conv_ms[handle->data_rate] + ADS1118_CONV_EXTRA_MS);

    /* 第二次传输（不启动新转换）读回刚完成的结果 */
    if (ads1118_transfer(handle, idle_cfg, &value, NULL) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    *raw = (int16_t)value;
    return DRIVER_OK;
}

int ads1118_read_raw(ads1118_handle_t *handle, ads1118_mux_t mux, int16_t *raw)
{
    if (handle == NULL || raw == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if ((uint16_t)mux > (uint16_t)ADS1118_MUX_SINGLE_3) {
        return (int)HAL_INVALID_PARAM;
    }

    return ads1118_convert_once(handle, ads1118_build_config(handle, mux, true),
                                ads1118_build_config(handle, mux, false), raw);
}

int ads1118_read_voltage(ads1118_handle_t *handle, ads1118_mux_t mux, float *volts)
{
    int16_t raw = 0;
    int ret;

    if (handle == NULL || volts == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = ads1118_read_raw(handle, mux, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *volts = (float)raw * (float)ads1118_fs_mv[handle->pga] / 32768.0f / 1000.0f;
    return DRIVER_OK;
}

int ads1118_read_internal_temp(ads1118_handle_t *handle, float *celsius)
{
    uint16_t start_cfg;
    uint16_t idle_cfg;
    int16_t raw = 0;
    int32_t value;
    int ret;

    if (handle == NULL || celsius == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    /* MUX=111 + TS_MODE=1 选择内部温度传感器 */
    start_cfg = ads1118_build_config(handle, ADS1118_MUX_SINGLE_3, true) | ADS1118_CFG_TS_MODE;
    idle_cfg = ads1118_build_config(handle, ADS1118_MUX_SINGLE_3, false) | ADS1118_CFG_TS_MODE;

    ret = ads1118_convert_once(handle, start_cfg, idle_cfg, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 结果左对齐 14 位，低 2 位无效；先符号扩展再做 4 分频 */
    value = (int32_t)raw;
    if (value >= 0x8000) {
        value -= 0x10000;
    }
    *celsius = (float)(value / 4) * ADS1118_TEMP_LSB_C;
    return DRIVER_OK;
}

int ads1118_read(ads1118_handle_t *handle, int16_t *values, size_t count)
{
    size_t i;

    if (handle == NULL || values == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (count == 0U || count > 4U) {
        return (int)HAL_INVALID_PARAM;
    }

    for (i = 0U; i < count; i++) {
        int ret = ads1118_read_raw(handle,
                                   (ads1118_mux_t)((uint16_t)ADS1118_MUX_SINGLE_0 + i),
                                   &values[i]);
        if (ret != DRIVER_OK) {
            return ret;
        }
    }
    return DRIVER_OK;
}
