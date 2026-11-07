#include "dht22.h"
#include "dht22_register.h"
#include "log.h"

/*
 * 引脚方向切换：输出（推挽）主动拉低产生起始信号；
 * 输入 + 上拉释放总线（由外部上拉拉高）并读取从机电平。
 * 不依赖开漏输出，可移植到没有 OD 能力的端口（如 AVR）。
 */
static int dht22_mode(dht22_handle_t *handle, port_gpio_mode_t mode)
{
    port_gpio_config_t cfg;

    cfg.mode = mode;
    cfg.output = PORT_GPIO_OUTPUT_PP;
    cfg.pull = (mode == PORT_GPIO_MODE_INPUT) ? PORT_GPIO_PULLUP : PORT_GPIO_NOPULL;
    cfg.speed = PORT_GPIO_SPEED_HIGH;
    cfg.alternate = 0U;
    cfg.initial_level = PORT_GPIO_LOW;

    return (port_gpio_init(handle->pin, &cfg) == PORT_OK) ? DRIVER_OK : (int)PORT_IO_ERROR;
}

/* 采样数据线：1 高 / 0 低，负值为错误码 */
static int dht22_line(dht22_handle_t *handle)
{
    port_gpio_level_t level = PORT_GPIO_LOW;

    if (port_gpio_read(handle->pin, &level) != PORT_OK) {
        return (int)PORT_IO_ERROR;
    }
    return (level == PORT_GPIO_HIGH) ? 1 : 0;
}

/* 等待总线到达指定电平，超时返回 PORT_TIMEOUT */
static int dht22_wait_level(dht22_handle_t *handle, int level, uint32_t timeout_us)
{
    uint32_t elapsed;
    int cur;

    for (elapsed = 0U; elapsed < timeout_us; elapsed++) {
        cur = dht22_line(handle);
        if (cur < 0) {
            return cur;
        }
        if (cur == level) {
            return DRIVER_OK;
        }
        driver_delay_us(1U);
    }
    return (int)PORT_TIMEOUT;
}

/* 读一个字节：每位先 50us 低电平，再按高电平宽度判 0/1（约 27us / 70us） */
static int dht22_read_byte(dht22_handle_t *handle, uint8_t *value)
{
    uint8_t data = 0U;
    uint32_t high_us;
    uint8_t i;
    int ret;
    int cur;

    for (i = 0U; i < 8U; i++) {
        ret = dht22_wait_level(handle, 0, DHT22_LEVEL_TIMEOUT_US);
        if (ret == DRIVER_OK) {
            ret = dht22_wait_level(handle, 1, DHT22_LEVEL_TIMEOUT_US);
        }
        if (ret != DRIVER_OK) {
            return ret;
        }
        for (high_us = 0U; high_us < DHT22_BIT_HIGH_TIMEOUT_US; high_us++) {
            cur = dht22_line(handle);
            if (cur < 0) {
                return cur;
            }
            if (cur == 0) {
                break;
            }
            driver_delay_us(1U);
        }
        data = (uint8_t)(data << 1);
        if (high_us > (uint32_t)DHT22_BIT1_MIN_HIGH_US) {
            data = (uint8_t)(data | 0x01U);
        }
    }
    *value = data;
    return DRIVER_OK;
}

int dht22_read_raw(dht22_handle_t *handle, uint8_t frame[5])
{
    uint8_t i;
    uint32_t now;
    int ret;

    if (handle == NULL || frame == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    /* 传感器采样周期 >=2s，过密读取会造成数据陈旧与自热累积 */
    now = driver_tick_ms();
    if (handle->sampled &&
        (uint32_t)(now - handle->last_read_ms) < DHT22_MIN_INTERVAL_MS) {
        return (int)PORT_BUSY;
    }
    /* 起始信号 + 40 位数据全程关中断，避免位时序被中断拉长 */
    port_irq_disable();
    (void)dht22_mode(handle, PORT_GPIO_MODE_OUTPUT);
    driver_delay_us(DHT22_START_LOW_US);
    ret = dht22_mode(handle, PORT_GPIO_MODE_INPUT);
    driver_delay_us(DHT22_RELEASE_US);
    /* 从机响应：低约 80us -> 高约 80us -> 低，随后即第 1 位的低电平 */
    if (ret == DRIVER_OK) {
        ret = dht22_wait_level(handle, 0, DHT22_LEVEL_TIMEOUT_US);
    }
    if (ret == DRIVER_OK) {
        ret = dht22_wait_level(handle, 1, DHT22_LEVEL_TIMEOUT_US);
    }
    if (ret == DRIVER_OK) {
        ret = dht22_wait_level(handle, 0, DHT22_LEVEL_TIMEOUT_US);
    }
    for (i = 0U; (i < DHT22_FRAME_BYTES) && (ret == DRIVER_OK); i++) {
        ret = dht22_read_byte(handle, &frame[i]);
    }
    port_irq_enable();

    if (ret != DRIVER_OK) {
        LOG_E("dht22", "frame error: %d", ret);
        return ret;
    }
    handle->last_read_ms = now;
    handle->sampled = true;
    return DRIVER_OK;
}

int dht22_init(dht22_handle_t *handle, port_pin_t pin)
{
    int ret;

    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->pin = pin;
    handle->last_read_ms = 0U;
    handle->sampled = false;
    handle->initialized = false;

    /* 空闲态：输入 + 上拉，总线应由上拉电阻保持高电平 */
    ret = dht22_mode(handle, PORT_GPIO_MODE_INPUT);
    if (ret != DRIVER_OK) {
        LOG_E("dht22", "gpio init failed: %d", ret);
        return ret;
    }
    handle->initialized = true;

    if (dht22_line(handle) != 1) {
        LOG_W("dht22", "bus idle low, check pull-up");
    }
    LOG_I("dht22", "init ok, pin %u", (unsigned int)pin);
    return DRIVER_OK;
}

int dht22_deinit(dht22_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)port_gpio_deinit(handle->pin);
    }
    handle->initialized = false;
    handle->sampled = false;
    return DRIVER_OK;
}

int dht22_read(dht22_handle_t *handle, float *temperature, float *humidity)
{
    uint8_t frame[DHT22_FRAME_BYTES];
    uint8_t sum;
    uint16_t raw_humi;
    int16_t raw_temp;
    float temp;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (temperature == NULL && humidity == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = dht22_read_raw(handle, frame);
    if (ret != DRIVER_OK) {
        return ret;
    }
    sum = (uint8_t)(frame[DHT22_IDX_HUMI_MSB] + frame[DHT22_IDX_HUMI_LSB] +
                    frame[DHT22_IDX_TEMP_MSB] + frame[DHT22_IDX_TEMP_LSB]);
    if (sum != frame[DHT22_IDX_CHECKSUM]) {
        LOG_E("dht22", "checksum fail 0x%02X != 0x%02X",
              (unsigned int)sum, (unsigned int)frame[DHT22_IDX_CHECKSUM]);
        return (int)PORT_CRC_ERROR;
    }

    raw_humi = (uint16_t)(((uint16_t)frame[DHT22_IDX_HUMI_MSB] << 8) |
                          (uint16_t)frame[DHT22_IDX_HUMI_LSB]);
    raw_temp = (int16_t)(((uint16_t)frame[DHT22_IDX_TEMP_MSB] << 8) |
                         (uint16_t)frame[DHT22_IDX_TEMP_LSB]);
    /* 温度 bit15 为符号位，低 15 位为幅值（非二进制补码） */
    temp = (float)((uint16_t)raw_temp & DHT22_TEMP_VALUE_MASK) * DHT22_TEMP_LSB;
    if (raw_temp < 0) {
        temp = -temp;
    }

    if (humidity != NULL) {
        *humidity = (float)raw_humi * DHT22_HUMI_LSB;
    }
    if (temperature != NULL) {
        *temperature = temp;
    }
    return DRIVER_OK;
}
