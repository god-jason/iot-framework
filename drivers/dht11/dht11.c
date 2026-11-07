#include "dht11.h"
#include "dht11_register.h"
#include "log.h"

/* 释放总线：切换为输入 + 上拉，由上拉电阻拉高 */
static void dht11_release(dht11_handle_t *handle)
{
    (void)driver_gpio_input(handle->pin, PORT_GPIO_PULLUP);
}

/* 主机拉低总线 */
static void dht11_drive_low(dht11_handle_t *handle)
{
    (void)driver_gpio_output(handle->pin, PORT_GPIO_LOW);
    (void)driver_gpio_write(handle->pin, PORT_GPIO_LOW);
}

static int dht11_line(dht11_handle_t *handle)
{
    int level = driver_gpio_read(handle->pin);

    return (level < 0) ? level : (level != 0) ? 1 : 0;
}

/* 等待总线到达指定电平，超时返回 PORT_TIMEOUT */
static int dht11_wait_level(dht11_handle_t *handle, int level, uint32_t timeout_us)
{
    uint32_t elapsed = 0U;
    int cur;

    while (elapsed < timeout_us) {
        cur = dht11_line(handle);
        if (cur < 0) {
            return cur;
        }
        if (cur == level) {
            return DRIVER_OK;
        }
        driver_delay_us(1U);
        elapsed++;
    }
    return (int)PORT_TIMEOUT;
}

/* 读取一个字节：以高电平持续时间区分 0 / 1 */
static int dht11_read_byte(dht11_handle_t *handle, uint8_t *value)
{
    uint8_t data = 0U;
    uint32_t high_us;
    uint8_t i;
    int ret;

    for (i = 0U; i < 8U; i++) {
        /* 每位以约 50us 低电平开始 */
        ret = dht11_wait_level(handle, 0, DHT11_TIMEOUT_US);
        if (ret == DRIVER_OK) {
            ret = dht11_wait_level(handle, 1, DHT11_TIMEOUT_US);
        }
        if (ret != DRIVER_OK) {
            return ret;
        }

        high_us = 0U;
        while (high_us < DHT11_BIT_TIMEOUT_US) {
            if (dht11_line(handle) != 1) {
                break;
            }
            driver_delay_us(1U);
            high_us++;
        }

        data = (uint8_t)(data << 1);
        if (high_us > DHT11_BIT1_HIGH_US) {
            data = (uint8_t)(data | 0x01U);
        }
    }

    *value = data;
    return DRIVER_OK;
}

int dht11_read_raw(dht11_handle_t *handle, uint8_t frame[5])
{
    uint8_t i;
    int ret;

    if (handle == NULL || frame == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    /* 起始信号 + 40 位数据：全程关闭中断，避免位时序被拉长 */
    port_irq_disable();

    dht11_drive_low(handle);
    driver_delay_ms(DHT11_START_LOW_MS);
    dht11_release(handle);
    driver_delay_us(DHT11_RELEASE_US);

    /* 从机响应：低约 80us 再高约 80us */
    ret = dht11_wait_level(handle, 0, DHT11_TIMEOUT_US);
    if (ret == DRIVER_OK) {
        ret = dht11_wait_level(handle, 1, DHT11_TIMEOUT_US);
    }
    if (ret == DRIVER_OK) {
        ret = dht11_wait_level(handle, 0, DHT11_TIMEOUT_US);
    }

    for (i = 0U; (i < DHT11_FRAME_BYTES) && (ret == DRIVER_OK); i++) {
        ret = dht11_read_byte(handle, &frame[i]);
    }

    port_irq_enable();
    dht11_release(handle);

    if (ret != DRIVER_OK) {
        LOG_E("dht11", "bus timeout");
        return (int)PORT_TIMEOUT;
    }
    return DRIVER_OK;
}

int dht11_init(dht11_handle_t *handle, port_pin_t pin)
{
    int ret;

    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->pin = pin;
    handle->last_read_ms = 0U;
    handle->initialized = false;

    ret = driver_gpio_input(pin, PORT_GPIO_PULLUP);
    if (ret != DRIVER_OK) {
        LOG_E("dht11", "gpio init failed: %d", ret);
        return ret;
    }

    /* 上电后总线应保持高电平（空闲态） */
    if (dht11_line(handle) != 1) {
        LOG_W("dht11", "bus idle low, check pull-up");
    }

    handle->initialized = true;
    LOG_I("dht11", "init ok, pin %u", (unsigned int)pin);
    return DRIVER_OK;
}

int dht11_deinit(dht11_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)driver_gpio_input(handle->pin, PORT_GPIO_PULLUP);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int dht11_read(dht11_handle_t *handle, float *temperature, float *humidity)
{
    uint8_t frame[DHT11_FRAME_BYTES];
    uint8_t sum;
    uint32_t now;
    float t;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (temperature == NULL && humidity == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    /* DHT11 采样间隔不得小于 1s */
    now = driver_tick_ms();
    if (handle->last_read_ms != 0U &&
        (uint32_t)(now - handle->last_read_ms) < DHT11_MIN_INTERVAL_MS) {
        return (int)PORT_BUSY;
    }

    ret = dht11_read_raw(handle, frame);
    if (ret != DRIVER_OK) {
        return ret;
    }

    sum = (uint8_t)(frame[DHT11_IDX_HUMI_INT] + frame[DHT11_IDX_HUMI_DEC] +
                    frame[DHT11_IDX_TEMP_INT] + frame[DHT11_IDX_TEMP_DEC]);
    if (sum != frame[DHT11_IDX_CHECKSUM]) {
        LOG_E("dht11", "checksum fail 0x%02X != 0x%02X", sum, frame[DHT11_IDX_CHECKSUM]);
        return (int)PORT_CRC_ERROR;
    }

    handle->last_read_ms = now;

    if (humidity != NULL) {
        /* 湿度 = 整数部分 + 小数部分（DHT11 小数恒为 0） */
        *humidity = (float)frame[DHT11_IDX_HUMI_INT] +
                    (float)(frame[DHT11_IDX_HUMI_DEC] & DHT11_DEC_MASK) * DHT11_DEC_SCALE;
    }
    if (temperature != NULL) {
        t = (float)frame[DHT11_IDX_TEMP_INT] +
            (float)(frame[DHT11_IDX_TEMP_DEC] & DHT11_DEC_MASK) * DHT11_DEC_SCALE;
        if ((frame[DHT11_IDX_TEMP_DEC] & DHT11_TEMP_SIGN) != 0U) {
            t = -t;   /* 兼容器件用小数位 bit7 表示负温 */
        }
        *temperature = t;
    }

    return DRIVER_OK;
}
