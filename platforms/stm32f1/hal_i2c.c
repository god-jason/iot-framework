/*
 * hal_i2c.c - STM32F1 I2C 寄存器实现（不依赖 Cube HAL）
 *
 * 支持 I2C1（PB6/PB7）、I2C2（PB10/PB11）7 位地址主机收发、
 * 寄存器读写、组合传输与设备探测；10 位地址暂不支持（返回 NOT_SUPPORTED）。
 * 引脚复用为 AF 开漏 50MHz，若板级使用重映射请在 board_init() 中另行配置。
 *
 * 编号：config.id = 0/1 依次对应 I2C1/I2C2（按芯片裁剪）。
 */

#if defined(ROS_STM32_F1)

#include "hal.h"
#include "stm32f10x.h"

#define STM32_I2C_CR1_PE        (1UL << 0)
#define STM32_I2C_CR1_START     (1UL << 8)
#define STM32_I2C_CR1_STOP      (1UL << 9)
#define STM32_I2C_CR1_ACK       (1UL << 10)
#define STM32_I2C_CR1_SWRST     (1UL << 15)

#define STM32_I2C_SR1_SB        (1UL << 0)
#define STM32_I2C_SR1_ADDR      (1UL << 1)
#define STM32_I2C_SR1_BTF       (1UL << 2)
#define STM32_I2C_SR1_RXNE      (1UL << 6)
#define STM32_I2C_SR1_TXE       (1UL << 7)
#define STM32_I2C_SR1_BERR      (1UL << 8)
#define STM32_I2C_SR1_ARLO      (1UL << 9)
#define STM32_I2C_SR1_AF        (1UL << 10)
#define STM32_I2C_SR1_OVR       (1UL << 11)

#define STM32_I2C_ERR_MASK      (STM32_I2C_SR1_BERR | STM32_I2C_SR1_ARLO | \
                                 STM32_I2C_SR1_AF)

static I2C_TypeDef *const s_stm32_i2c_inst[] = {
    I2C1,
#ifdef I2C2
    I2C2,
#endif
};

#define STM32_I2C_CH_COUNT \
    (sizeof(s_stm32_i2c_inst) / sizeof(s_stm32_i2c_inst[0]))

static uint32_t stm32_pclk1(void)
{
    uint32_t pre = (RCC->CFGR >> 8) & 0x7U;

    switch (pre) {
    case 4U: return SystemCoreClock / 2U;
    case 5U: return SystemCoreClock / 4U;
    case 6U: return SystemCoreClock / 8U;
    case 7U: return SystemCoreClock / 16U;
    default: return SystemCoreClock;
    }
}

static void stm32_i2c_pins_init(I2C_TypeDef *i2c)
{
    RCC->APB2ENR |= (1UL << 3);     /* GPIOB */

    if (i2c == I2C1) {
        /* PB6/PB7 -> AF 开漏 50MHz */
        GPIOB->CRL = (GPIOB->CRL & ~(0xFFUL << 24)) | (0xFFUL << 24);
        GPIOB->BSRR = (1UL << 6) | (1UL << 7);
    }
#ifdef I2C2
    else if (i2c == I2C2) {
        /* PB10/PB11 -> AF 开漏 50MHz */
        GPIOB->CRH = (GPIOB->CRH & ~(0xFFUL << 8)) | (0xFFUL << 8);
        GPIOB->BSRR = (1UL << 10) | (1UL << 11);
    }
#endif
}

static int stm32_i2c_wait(I2C_TypeDef *i2c, uint32_t mask, bool set,
                          uint32_t timeout)
{
    uint32_t start = hal_tick_get();

    while ((((i2c->SR1 & mask) != 0U) != set)) {
        if ((i2c->SR1 & STM32_I2C_ERR_MASK) != 0U) {
            return HAL_IO_ERROR;
        }
        if (timeout != HAL_TIMEOUT_FOREVER &&
            ((uint32_t)(hal_tick_get() - start) >= timeout)) {
            return HAL_TIMEOUT;
        }
    }
    return HAL_OK;
}

static void stm32_i2c_clear_addr(I2C_TypeDef *i2c)
{
    (void)i2c->SR1;
    (void)i2c->SR2;
}

static int stm32_i2c_start(I2C_TypeDef *i2c, uint32_t timeout)
{
    i2c->CR1 |= STM32_I2C_CR1_START;
    return stm32_i2c_wait(i2c, STM32_I2C_SR1_SB, true, timeout);
}

static int stm32_i2c_send_address(I2C_TypeDef *i2c, uint16_t address,
                                  bool read, bool ack, uint32_t timeout)
{
    int result;

    i2c->DR = (uint8_t)((address << 1) | (read ? 1U : 0U));
    result = stm32_i2c_wait(i2c, STM32_I2C_SR1_ADDR, true, timeout);
    if (result == HAL_OK) {
        /* 单字节接收必须在清除 ADDR 前撤掉 ACK */
        if (read) {
            if (ack) {
                i2c->CR1 |= STM32_I2C_CR1_ACK;
            } else {
                i2c->CR1 &= ~STM32_I2C_CR1_ACK;
            }
        }
        stm32_i2c_clear_addr(i2c);
    }
    return result;
}

static int stm32_i2c_send_bytes(I2C_TypeDef *i2c, const uint8_t *data,
                                size_t length, uint32_t timeout)
{
    size_t i;
    int result;

    for (i = 0U; i < length; i++) {
        result = stm32_i2c_wait(i2c, STM32_I2C_SR1_TXE, true, timeout);
        if (result != HAL_OK) {
            return result;
        }
        i2c->DR = data[i];
    }
    return HAL_OK;
}

static int stm32_i2c_receive_bytes(I2C_TypeDef *i2c, uint8_t *data,
                                   size_t length, uint32_t timeout)
{
    size_t i;
    int result;

    for (i = 0U; i < length; i++) {
        if ((length > 1U) && (i == (length - 2U))) {
            i2c->CR1 &= ~STM32_I2C_CR1_ACK;
        }
        result = stm32_i2c_wait(i2c, STM32_I2C_SR1_RXNE, true, timeout);
        if (result != HAL_OK) {
            return result;
        }
        data[i] = (uint8_t)i2c->DR;
    }
    return HAL_OK;
}

static void stm32_i2c_stop(I2C_TypeDef *i2c)
{
    i2c->CR1 |= STM32_I2C_CR1_STOP;
}

int hal_i2c_init(hal_i2c_t *handle, const hal_i2c_config_t *config)
{
    I2C_TypeDef *i2c;
    uint32_t pclk;
    uint32_t speed;
    uint32_t ccr;
    uint32_t trise;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    if (config->address_mode == HAL_I2C_ADDR_10BIT) {
        return HAL_NOT_SUPPORTED;
    }
    if (config->speed == HAL_I2C_SPEED_FAST_PLUS) {
        return HAL_NOT_SUPPORTED;
    }

    if (config->id >= STM32_I2C_CH_COUNT) {
        return HAL_INVALID_PARAM;
    }
    i2c = s_stm32_i2c_inst[config->id];
    handle->base.instance = i2c;
    if (i2c == I2C1) {
        RCC->APB1ENR |= (1UL << 21);
    }
#ifdef I2C2
    else if (i2c == I2C2) {
        RCC->APB1ENR |= (1UL << 22);
    }
#endif
    else {
        return HAL_INVALID_PARAM;
    }

    stm32_i2c_pins_init(i2c);
    pclk = stm32_pclk1();
    speed = (config->speed == HAL_I2C_SPEED_FAST) ? 400000U : 100000U;

    /* 复位 I2C 外设 */
    i2c->CR1 |= STM32_I2C_CR1_SWRST;
    i2c->CR1 &= ~STM32_I2C_CR1_SWRST;

    i2c->CR2 = pclk / 1000000U;    /* FREQ，单位 MHz */

    if (speed <= 100000U) {
        ccr = pclk / (speed * 2U);
        if (ccr < 4U) {
            ccr = 4U;
        }
        trise = (pclk / 1000000U) + 1U;
    } else {
        ccr = pclk / (speed * 3U);
        if (ccr < 1U) {
            ccr = 1U;
        }
        trise = ((pclk / 1000000U) * 300U) / 1000U + 1U;
    }

    i2c->CCR = ccr;                /* 标准/快速模式，DUTY=0 */
    i2c->TRISE = trise & 0x3FU;

    i2c->CR1 |= STM32_I2C_CR1_PE;
    if (config->mode == HAL_I2C_MODE_MASTER) {
        i2c->CR1 |= STM32_I2C_CR1_ACK;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_i2c_deinit(hal_i2c_t *handle)
{
    I2C_TypeDef *i2c;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    i2c = (I2C_TypeDef *)handle->base.instance;
    if (i2c != NULL) {
        i2c->CR1 &= ~STM32_I2C_CR1_PE;
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_i2c_master_transmit(hal_i2c_t *handle, uint16_t address,
                            const uint8_t *data, size_t length,
                            uint32_t timeout)
{
    I2C_TypeDef *i2c;
    int result;

    if ((handle == NULL) || (data == NULL) || (length == 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    i2c = (I2C_TypeDef *)handle->base.instance;
    if (i2c == NULL) {
        return HAL_NOT_READY;
    }

    handle->base.state = HAL_STATE_BUSY_TX;
    result = stm32_i2c_start(i2c, timeout);
    if (result == HAL_OK) {
        result = stm32_i2c_send_address(i2c, address, false, true, timeout);
    }
    if (result == HAL_OK) {
        result = stm32_i2c_send_bytes(i2c, data, length, timeout);
    }
    if (result == HAL_OK) {
        result = stm32_i2c_wait(i2c, STM32_I2C_SR1_BTF, true, timeout);
    }
    stm32_i2c_stop(i2c);

    handle->base.state = (result == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    handle->base.error = (result == HAL_OK) ? HAL_OK : result;
    return result;
}

int hal_i2c_master_receive(hal_i2c_t *handle, uint16_t address,
                           uint8_t *data, size_t length, uint32_t timeout)
{
    I2C_TypeDef *i2c;
    int result;

    if ((handle == NULL) || (data == NULL) || (length == 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    i2c = (I2C_TypeDef *)handle->base.instance;
    if (i2c == NULL) {
        return HAL_NOT_READY;
    }

    handle->base.state = HAL_STATE_BUSY_RX;
    i2c->CR1 |= STM32_I2C_CR1_ACK;
    result = stm32_i2c_start(i2c, timeout);
    if (result == HAL_OK) {
        result = stm32_i2c_send_address(i2c, address, true, (length > 1U), timeout);
    }
    if (result == HAL_OK) {
        result = stm32_i2c_receive_bytes(i2c, data, length, timeout);
    }
    stm32_i2c_stop(i2c);
    i2c->CR1 |= STM32_I2C_CR1_ACK;

    handle->base.state = (result == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    handle->base.error = (result == HAL_OK) ? HAL_OK : result;
    return result;
}

int hal_i2c_master_transfer(hal_i2c_t *handle, uint16_t address,
                            const uint8_t *tx_data, size_t tx_length,
                            uint8_t *rx_data, size_t rx_length,
                            uint32_t timeout)
{
    I2C_TypeDef *i2c;
    int result;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if ((tx_length == 0U) && (rx_length == 0U)) {
        return HAL_INVALID_PARAM;
    }
    i2c = (I2C_TypeDef *)handle->base.instance;
    if (i2c == NULL) {
        return HAL_NOT_READY;
    }

    handle->base.state = HAL_STATE_BUSY_TX_RX;
    i2c->CR1 |= STM32_I2C_CR1_ACK;
    result = stm32_i2c_start(i2c, timeout);

    if ((result == HAL_OK) && (tx_length > 0U)) {
        result = stm32_i2c_send_address(i2c, address, false, true, timeout);
        if (result == HAL_OK) {
            result = stm32_i2c_send_bytes(i2c, tx_data, tx_length, timeout);
        }
    }

    if ((result == HAL_OK) && (rx_length > 0U)) {
        if (tx_length > 0U) {
            /* Repeated Start */
            result = stm32_i2c_start(i2c, timeout);
        }
        if (result == HAL_OK) {
            result = stm32_i2c_send_address(i2c, address, true, (rx_length > 1U), timeout);
        }
        if (result == HAL_OK) {
            result = stm32_i2c_receive_bytes(i2c, rx_data, rx_length, timeout);
        }
    }

    stm32_i2c_stop(i2c);
    i2c->CR1 |= STM32_I2C_CR1_ACK;
    handle->base.state = (result == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    handle->base.error = (result == HAL_OK) ? HAL_OK : result;
    return result;
}

int hal_i2c_mem_write(hal_i2c_t *handle, uint16_t address, uint16_t reg,
                      uint8_t reg_size, const uint8_t *data, size_t length,
                      uint32_t timeout)
{
    uint8_t reg_buf[4];
    I2C_TypeDef *i2c;
    int result;
    uint8_t i;

    if ((handle == NULL) || (data == NULL) || (reg_size == 0U) ||
        (reg_size > 4U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    i2c = (I2C_TypeDef *)handle->base.instance;
    if (i2c == NULL) {
        return HAL_NOT_READY;
    }

    for (i = 0U; i < reg_size; i++) {
        reg_buf[i] = (uint8_t)(reg >> (8U * (reg_size - 1U - i)));
    }

    handle->base.state = HAL_STATE_BUSY_TX;
    result = stm32_i2c_start(i2c, timeout);
    if (result == HAL_OK) {
        result = stm32_i2c_send_address(i2c, address, false, true, timeout);
    }
    if (result == HAL_OK) {
        result = stm32_i2c_send_bytes(i2c, reg_buf, reg_size, timeout);
    }
    if (result == HAL_OK) {
        result = stm32_i2c_send_bytes(i2c, data, length, timeout);
    }
    if (result == HAL_OK) {
        result = stm32_i2c_wait(i2c, STM32_I2C_SR1_BTF, true, timeout);
    }
    stm32_i2c_stop(i2c);

    handle->base.state = (result == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    handle->base.error = (result == HAL_OK) ? HAL_OK : result;
    return result;
}

int hal_i2c_mem_read(hal_i2c_t *handle, uint16_t address, uint16_t reg,
                     uint8_t reg_size, uint8_t *data, size_t length,
                     uint32_t timeout)
{
    uint8_t reg_buf[4];
    I2C_TypeDef *i2c;
    int result;
    uint8_t i;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (reg_size == 0U) || (reg_size > 4U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    i2c = (I2C_TypeDef *)handle->base.instance;
    if (i2c == NULL) {
        return HAL_NOT_READY;
    }

    for (i = 0U; i < reg_size; i++) {
        reg_buf[i] = (uint8_t)(reg >> (8U * (reg_size - 1U - i)));
    }

    handle->base.state = HAL_STATE_BUSY_TX_RX;
    i2c->CR1 |= STM32_I2C_CR1_ACK;
    result = stm32_i2c_start(i2c, timeout);
    if (result == HAL_OK) {
        result = stm32_i2c_send_address(i2c, address, false, true, timeout);
    }
    if (result == HAL_OK) {
        result = stm32_i2c_send_bytes(i2c, reg_buf, reg_size, timeout);
    }
    if (result == HAL_OK) {
        result = stm32_i2c_start(i2c, timeout);
    }
    if (result == HAL_OK) {
        result = stm32_i2c_send_address(i2c, address, true, (length > 1U), timeout);
    }
    if (result == HAL_OK) {
        result = stm32_i2c_receive_bytes(i2c, data, length, timeout);
    }
    stm32_i2c_stop(i2c);
    i2c->CR1 |= STM32_I2C_CR1_ACK;

    handle->base.state = (result == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    handle->base.error = (result == HAL_OK) ? HAL_OK : result;
    return result;
}

int hal_i2c_master_transmit_dma(hal_i2c_t *handle, uint16_t address,
                                const uint8_t *data, size_t length)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(address);
    HAL_UNUSED(data);
    HAL_UNUSED(length);
    return HAL_NOT_SUPPORTED;
}

int hal_i2c_master_receive_dma(hal_i2c_t *handle, uint16_t address,
                               uint8_t *data, size_t length)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(address);
    HAL_UNUSED(data);
    HAL_UNUSED(length);
    return HAL_NOT_SUPPORTED;
}

int hal_i2c_is_device_ready(hal_i2c_t *handle, uint16_t address,
                            uint32_t trials, uint32_t timeout)
{
    I2C_TypeDef *i2c;
    uint32_t attempt;
    int result = HAL_IO_ERROR;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    i2c = (I2C_TypeDef *)handle->base.instance;
    if (i2c == NULL) {
        return HAL_NOT_READY;
    }
    if (trials == 0U) {
        trials = 1U;
    }

    for (attempt = 0U; attempt < trials; attempt++) {
        result = stm32_i2c_start(i2c, timeout);
        if (result != HAL_OK) {
            break;
        }
        i2c->DR = (uint8_t)(address << 1);
        result = stm32_i2c_wait(i2c, STM32_I2C_SR1_ADDR, true, timeout);
        if (result == HAL_OK) {
            stm32_i2c_clear_addr(i2c);
            stm32_i2c_stop(i2c);
            return HAL_OK;
        }
        /* 检查是否因 NACK 失败 */
        if ((i2c->SR1 & STM32_I2C_SR1_AF) != 0U) {
            i2c->SR1 &= ~STM32_I2C_SR1_AF;
            result = HAL_IO_ERROR;
        }
        stm32_i2c_stop(i2c);
        if (result != HAL_IO_ERROR) {
            break;
        }
    }
    return result;
}

int hal_i2c_bus_recover(hal_i2c_t *handle)
{
    I2C_TypeDef *i2c;
    uint32_t i;
    uint32_t scl_mask;
    uint32_t sda_mask;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    i2c = (I2C_TypeDef *)handle->base.instance;
    if (i2c == NULL) {
        return HAL_NOT_READY;
    }
    if (i2c == I2C1) {
        scl_mask = 1UL << 6;
        sda_mask = 1UL << 7;
    }
#ifdef I2C2
    else if (i2c == I2C2) {
        scl_mask = 1UL << 10;
        sda_mask = 1UL << 11;
    }
#endif
    else {
        return HAL_INVALID_PARAM;
    }

    i2c->CR1 &= ~STM32_I2C_CR1_PE;

    /* SCL/SDA 切为通用开漏输出（CNF=01, MODE=10） */
    if (i2c == I2C1) {
        GPIOB->CRL = (GPIOB->CRL & ~(0xFFUL << 24)) | (0x66UL << 24);
    } else {
        GPIOB->CRH = (GPIOB->CRH & ~(0xFFUL << 8)) | (0x66UL << 8);
    }
    GPIOB->BSRR = scl_mask | sda_mask;

    for (i = 0U; i < 9U; i++) {
        GPIOB->BSRR = (scl_mask << 16);
        hal_delay_ms(1U);
        GPIOB->BSRR = scl_mask;
        hal_delay_ms(1U);
    }
    /* 生成 STOP：SCL 高时 SDA 由低到高 */
    GPIOB->BSRR = (sda_mask << 16);
    hal_delay_ms(1U);
    GPIOB->BSRR = scl_mask;
    hal_delay_ms(1U);
    GPIOB->BSRR = sda_mask;
    hal_delay_ms(1U);

    /* 恢复 I2C 复用配置并重新初始化 */
    stm32_i2c_pins_init(i2c);
    handle->base.state = HAL_STATE_RESET;
    return hal_i2c_init(handle, &handle->config);
}

int hal_i2c_abort(hal_i2c_t *handle)
{
    I2C_TypeDef *i2c;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    i2c = (I2C_TypeDef *)handle->base.instance;
    if (i2c == NULL) {
        return HAL_NOT_READY;
    }
    stm32_i2c_stop(i2c);
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

hal_state_e hal_i2c_get_state(hal_i2c_t *handle)
{
    if (handle == NULL) {
        return HAL_STATE_RESET;
    }
    return handle->base.state;
}

#endif /* ROS_STM32_F1 */
