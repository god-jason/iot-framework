/* 主机模拟平台总线类外设单元测试。
 * 覆盖 GPIO/EXTI、UART、SPI、I2C、I2S、CAN、DMA。
 * 每个子用例先 port_sim_reset_state()，再重新初始化对应句柄。 */
#include "test_framework.h"
#include "port_sim.h"

#include <string.h>

static int s_marker;
static int s_exti_count;
static port_pin_t s_exti_last_pin;
static void *s_exti_user_data;
static int s_uart_tx_count;
static int s_uart_rx_count;
static int s_uart_idle_count;
static int s_can_rx_count;
static void *s_can_user_data;
static int s_dma_complete_count;

static void exti_cb(port_pin_t pin, void *user_data)
{
    s_exti_count++;
    s_exti_last_pin = pin;
    s_exti_user_data = user_data;
}

static void uart_cb(void *handle, port_uart_event_t event, size_t length, void *user_data)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(length);
    PORT_UNUSED(user_data);
    switch (event) {
    case PORT_UART_EVENT_TX_COMPLETE:
        s_uart_tx_count++;
        break;
    case PORT_UART_EVENT_RX_COMPLETE:
        s_uart_rx_count++;
        break;
    case PORT_UART_EVENT_RX_IDLE:
        s_uart_idle_count++;
        break;
    default:
        break;
    }
}

static void can_cb(void *handle, port_event_t event, void *user_data)
{
    PORT_UNUSED(handle);
    if (event == PORT_EVENT_RX_COMPLETE) {
        s_can_rx_count++;
        s_can_user_data = user_data;
    }
}

static void dma_cb(void *handle, port_event_t event, void *user_data)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(user_data);
    if (event == PORT_EVENT_COMPLETE) {
        s_dma_complete_count++;
    }
}

static void test_gpio(void)
{
    port_gpio_config_t config;
    port_gpio_level_t level;

    port_sim_reset_state();
    tf_begin("sim/gpio 初始化、读写与翻转");
    memset(&config, 0, sizeof(config));
    config.mode = PORT_GPIO_MODE_OUTPUT;
    config.output = PORT_GPIO_OUTPUT_PP;
    config.initial_level = PORT_GPIO_HIGH;

    TF_CHECK_INT(port_gpio_init(PORT_PIN(1, 2), &config), PORT_OK);
    TF_CHECK_INT(port_gpio_read(PORT_PIN(1, 2), &level), PORT_OK);
    TF_CHECK_INT(level, PORT_GPIO_HIGH);

    TF_CHECK_INT(port_gpio_write(PORT_PIN(1, 2), PORT_GPIO_LOW), PORT_OK);
    TF_CHECK_INT(port_gpio_read(PORT_PIN(1, 2), &level), PORT_OK);
    TF_CHECK_INT(level, PORT_GPIO_LOW);

    TF_CHECK_INT(port_gpio_toggle(PORT_PIN(1, 2)), PORT_OK);
    TF_CHECK_INT(port_gpio_read(PORT_PIN(1, 2), &level), PORT_OK);
    TF_CHECK_INT(level, PORT_GPIO_HIGH);

    TF_CHECK_INT(port_gpio_reset(PORT_PIN(1, 2)), PORT_OK);
    TF_CHECK_INT(port_gpio_read(PORT_PIN(1, 2), &level), PORT_OK);
    TF_CHECK_INT(level, PORT_GPIO_LOW);

    TF_CHECK_INT(port_gpio_set(PORT_PIN(1, 2)), PORT_OK);
    TF_CHECK_INT(port_gpio_read(PORT_PIN(1, 2), &level), PORT_OK);
    TF_CHECK_INT(level, PORT_GPIO_HIGH);

    TF_CHECK_INT(port_gpio_deinit(PORT_PIN(1, 2)), PORT_OK);
    tf_end();

    tf_begin("sim/gpio 非法参数");
    TF_CHECK_INT(port_gpio_init(PORT_PIN(16, 0), &config), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_gpio_init(PORT_PIN(0, 32), &config), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_gpio_init(PORT_PIN(0, 0), NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_gpio_write(PORT_PIN(16, 0), PORT_GPIO_HIGH), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_gpio_write(PORT_PIN(0, 0), (port_gpio_level_t)2), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_gpio_read(PORT_PIN(0, 32), &level), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_gpio_read(PORT_PIN(0, 0), NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_gpio_toggle(PORT_PIN(16, 0)), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_gpio_set(PORT_PIN(0, 32)), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_gpio_reset(PORT_PIN(16, 0)), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_gpio_deinit(PORT_PIN(16, 0)), PORT_INVALID_PARAM);
    tf_end();
}

static void test_exti(void)
{
    port_exti_config_t config;

    port_sim_reset_state();
    tf_begin("sim/exti 回调、禁用与反初始化");
    memset(&config, 0, sizeof(config));
    config.trigger = PORT_EXTI_RISING;
    config.priority = 1U;
    config.callback = exti_cb;
    config.user_data = &s_marker;

    s_exti_count = 0;
    s_exti_last_pin = 0U;
    s_exti_user_data = NULL;

    TF_CHECK_INT(port_exti_init(PORT_PIN(2, 3), &config), PORT_OK);
    port_sim_exti_trigger(PORT_PIN(2, 3));
    TF_CHECK_INT(s_exti_count, 1);
    TF_CHECK_INT(s_exti_last_pin, PORT_PIN(2, 3));
    TF_CHECK(s_exti_user_data == &s_marker);

    TF_CHECK_INT(port_exti_disable(PORT_PIN(2, 3)), PORT_OK);
    port_sim_exti_trigger(PORT_PIN(2, 3));
    TF_CHECK_INT(s_exti_count, 1);

    TF_CHECK_INT(port_exti_enable(PORT_PIN(2, 3)), PORT_OK);
    port_sim_exti_trigger(PORT_PIN(2, 3));
    TF_CHECK_INT(s_exti_count, 2);

    TF_CHECK_INT(port_exti_clear(PORT_PIN(2, 3)), PORT_OK);
    TF_CHECK_INT(port_exti_deinit(PORT_PIN(2, 3)), PORT_OK);
    port_sim_exti_trigger(PORT_PIN(2, 3));
    TF_CHECK_INT(s_exti_count, 2);

    TF_CHECK_INT(port_exti_enable(PORT_PIN(4, 1)), PORT_NOT_READY);
    TF_CHECK_INT(port_exti_init(PORT_PIN(16, 0), &config), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_exti_init(PORT_PIN(0, 0), NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_exti_enable(PORT_PIN(0, 32)), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_exti_disable(PORT_PIN(16, 0)), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_exti_deinit(PORT_PIN(0, 32)), PORT_INVALID_PARAM);
    tf_end();
}

static void test_uart(void)
{
    port_uart_handle_t uart;
    port_uart_handle_t idle;
    port_uart_config_t config;
    uint8_t tx[4] = { 'A', 'B', 'C', 'D' };
    uint8_t rx[8];
    uint32_t n;

    port_sim_reset_state();
    memset(&uart, 0, sizeof(uart));
    memset(&config, 0, sizeof(config));
    config.baudrate = 115200U;
    config.word_length = PORT_UART_WORD_8BIT;
    config.parity = PORT_UART_PARITY_NONE;
    config.stop_bits = PORT_UART_STOP_1;
    config.mode = PORT_UART_MODE_TX_RX;

    tf_begin("sim/uart 初始化与阻塞收发");
    TF_CHECK_INT(port_uart_init(NULL, &config), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_uart_init(&uart, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_uart_init(&uart, &config), PORT_OK);

    TF_CHECK_INT(port_uart_transmit(&uart, NULL, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_uart_transmit(&uart, tx, 4U, 100U), PORT_OK);

    memset(rx, 0, sizeof(rx));
    n = port_sim_uart_get_tx(&uart, rx, sizeof(rx));
    TF_CHECK_INT(n, 4U);
    TF_CHECK_MEM(rx, tx, 4U);

    TF_CHECK_INT(port_sim_uart_inject(&uart, tx, 4U), 4U);
    TF_CHECK(port_uart_get_rx_count(&uart) > 0U);
    memset(rx, 0, sizeof(rx));
    TF_CHECK_INT(port_uart_receive(&uart, rx, 4U, 100U), PORT_OK);
    TF_CHECK_MEM(rx, tx, 4U);

    /* 缓冲已空 */
    TF_CHECK_INT(port_uart_receive(&uart, rx, 4U, 0U), PORT_TIMEOUT);

    /* 接收至空闲 */
    TF_CHECK_INT(port_sim_uart_inject(&uart, tx, 3U), 3U);
    memset(rx, 0, sizeof(rx));
    TF_CHECK_INT(port_uart_receive_to_idle(&uart, rx, sizeof(rx), PORT_MODE_IT), PORT_OK);
    TF_CHECK_MEM(rx, tx, 3U);
    tf_end();

    tf_begin("sim/uart 中断/DMA 回调与中止");
    s_uart_tx_count = 0;
    s_uart_rx_count = 0;
    s_uart_idle_count = 0;
    TF_CHECK_INT(port_uart_register_callback(&uart, uart_cb, &s_marker), PORT_OK);

    TF_CHECK_INT(port_uart_transmit_it(&uart, tx, 2U), PORT_OK);
    TF_CHECK_INT(s_uart_tx_count, 1);
    TF_CHECK_INT(port_uart_transmit_dma(&uart, tx, 2U), PORT_OK);
    TF_CHECK_INT(s_uart_tx_count, 2);

    TF_CHECK_INT(port_sim_uart_inject(&uart, tx, 2U), 2U);
    TF_CHECK_INT(port_uart_receive_it(&uart, rx, 2U), PORT_OK);
    TF_CHECK_INT(s_uart_rx_count, 1);

    TF_CHECK_INT(port_sim_uart_inject(&uart, tx, 2U), 2U);
    TF_CHECK_INT(port_uart_receive_dma(&uart, rx, 2U), PORT_OK);
    TF_CHECK_INT(s_uart_rx_count, 2);

    TF_CHECK_INT(port_uart_abort(&uart), PORT_OK);
    memset(rx, 0, sizeof(rx));
    TF_CHECK_INT(port_sim_uart_get_tx(&uart, rx, sizeof(rx)), 0U);
    tf_end();

    tf_begin("sim/uart 未初始化与反初始化");
    memset(&idle, 0, sizeof(idle));
    TF_CHECK_INT(port_uart_transmit(&idle, tx, 1U, 0U), PORT_NOT_READY);
    TF_CHECK_INT(port_uart_receive(&idle, rx, 1U, 0U), PORT_NOT_READY);
    TF_CHECK_INT(port_uart_receive_to_idle(&idle, rx, 1U, PORT_MODE_BLOCKING), PORT_NOT_READY);
    TF_CHECK_INT(port_uart_get_rx_count(&idle), 0U);

    TF_CHECK_INT(port_uart_transmit(NULL, tx, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_uart_receive(NULL, rx, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_uart_abort(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_uart_register_callback(NULL, uart_cb, NULL), PORT_INVALID_PARAM);

    TF_CHECK_INT(port_uart_deinit(&uart), PORT_OK);
    TF_CHECK_INT(port_uart_transmit(&uart, tx, 1U, 0U), PORT_NOT_READY);
    TF_CHECK_INT(port_uart_receive(&uart, rx, 1U, 0U), PORT_NOT_READY);
    tf_end();
}

static void test_spi(void)
{
    port_spi_handle_t spi;
    port_spi_handle_t idle;
    port_spi_config_t config;
    uint8_t tx[4] = { 1U, 2U, 3U, 4U };
    uint8_t rx[4];
    int i;

    port_sim_reset_state();
    memset(&spi, 0, sizeof(spi));
    memset(&config, 0, sizeof(config));
    config.mode = PORT_SPI_MODE_MASTER;
    config.baudrate = 1000000U;
    config.data_bits = 8U;

    tf_begin("sim/spi 收发、回环与速率");
    TF_CHECK_INT(port_spi_init(NULL, &config), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_spi_init(&spi, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_spi_init(&spi, &config), PORT_OK);

    TF_CHECK_INT(port_spi_transmit(&spi, NULL, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_spi_transmit(&spi, tx, sizeof(tx), 0U), PORT_OK);
    TF_CHECK_INT(port_spi_transmit(&spi, NULL, 0U, 0U), PORT_OK);

    memset(rx, 0xFF, sizeof(rx));
    TF_CHECK_INT(port_spi_receive(&spi, rx, sizeof(rx), 0U), PORT_OK);
    for (i = 0; i < (int)sizeof(rx); i++) {
        TF_CHECK_INT(rx[i], 0U);
    }
    TF_CHECK_INT(port_spi_receive(&spi, NULL, 1U, 0U), PORT_INVALID_PARAM);

    memset(rx, 0, sizeof(rx));
    TF_CHECK_INT(port_spi_transfer(&spi, tx, rx, sizeof(tx), 0U), PORT_OK);
    TF_CHECK_MEM(rx, tx, sizeof(tx));
    TF_CHECK_INT(port_spi_transfer(&spi, NULL, NULL, 0U, 0U), PORT_OK);
    TF_CHECK_INT(port_spi_transfer(&spi, NULL, rx, 4U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_spi_transfer(&spi, tx, NULL, 4U, 0U), PORT_INVALID_PARAM);

    memset(rx, 0, sizeof(rx));
    TF_CHECK_INT(port_spi_transfer_dma(&spi, tx, rx, sizeof(tx)), PORT_OK);
    TF_CHECK_MEM(rx, tx, sizeof(tx));
    TF_CHECK_INT(port_spi_transmit_dma(&spi, tx, sizeof(tx)), PORT_OK);
    TF_CHECK_INT(port_spi_receive_dma(&spi, rx, sizeof(rx)), PORT_OK);

    TF_CHECK_INT(port_spi_set_speed(&spi, 2000000U), PORT_OK);
    TF_CHECK_INT(spi.config.baudrate, 2000000U);

    TF_CHECK_INT(port_spi_abort(&spi), PORT_OK);
    TF_CHECK_INT(port_spi_get_state(&spi), PORT_STATE_READY);
    tf_end();

    tf_begin("sim/spi 未初始化与反初始化");
    memset(&idle, 0, sizeof(idle));
    TF_CHECK_INT(port_spi_transmit(&idle, tx, 1U, 0U), PORT_NOT_READY);
    TF_CHECK_INT(port_spi_receive(&idle, rx, 1U, 0U), PORT_NOT_READY);
    TF_CHECK_INT(port_spi_transfer(&idle, tx, rx, 1U, 0U), PORT_NOT_READY);
    TF_CHECK_INT(port_spi_set_speed(&idle, 1000U), PORT_NOT_READY);
    TF_CHECK_INT(port_spi_get_state(NULL), PORT_STATE_RESET);

    TF_CHECK_INT(port_spi_transmit(NULL, tx, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_spi_set_speed(NULL, 1000U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_spi_abort(NULL), PORT_INVALID_PARAM);

    TF_CHECK_INT(port_spi_deinit(&spi), PORT_OK);
    TF_CHECK_INT(port_spi_transfer(&spi, tx, rx, 1U, 0U), PORT_NOT_READY);
    tf_end();
}

static void test_i2c(void)
{
    port_i2c_handle_t i2c;
    port_i2c_handle_t idle;
    port_i2c_config_t config;
    uint8_t data[4];
    uint8_t rx[4];
    uint8_t reg_only[1];
    uint8_t expect[2];

    port_sim_reset_state();
    memset(&i2c, 0, sizeof(i2c));
    memset(&config, 0, sizeof(config));
    config.mode = PORT_I2C_MODE_MASTER;
    config.speed = PORT_I2C_SPEED_STANDARD;
    config.address_mode = PORT_I2C_ADDR_7BIT;

    tf_begin("sim/i2c 主机寄存器收发");
    TF_CHECK_INT(port_i2c_init(NULL, &config), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2c_init(&i2c, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2c_init(&i2c, &config), PORT_OK);

    /* data[0] 为寄存器地址，后续字节依次写入 */
    data[0] = 0x10U;
    data[1] = 0xAAU;
    data[2] = 0xBBU;
    TF_CHECK_INT(port_i2c_master_transmit(&i2c, 0x50U, data, 3U, 100U), PORT_OK);

    expect[0] = 0xAAU;
    expect[1] = 0xBBU;
    memset(rx, 0, sizeof(rx));
    TF_CHECK_INT(port_i2c_mem_read(&i2c, 0x50U, 0x10U, 1U, rx, 2U, 100U), PORT_OK);
    TF_CHECK_MEM(rx, expect, 2U);

    /* 复位内部寄存器指针后使用 master_receive 读回 */
    reg_only[0] = 0x10U;
    TF_CHECK_INT(port_i2c_master_transmit(&i2c, 0x50U, reg_only, 1U, 100U), PORT_OK);
    memset(rx, 0, sizeof(rx));
    TF_CHECK_INT(port_i2c_master_receive(&i2c, 0x50U, rx, 2U, 100U), PORT_OK);
    TF_CHECK_MEM(rx, expect, 2U);

    /* mem_write / mem_read 往返 */
    data[0] = 0x11U;
    data[1] = 0x22U;
    TF_CHECK_INT(port_i2c_mem_write(&i2c, 0x50U, 0x20U, 1U, data, 2U, 100U), PORT_OK);
    memset(rx, 0, sizeof(rx));
    TF_CHECK_INT(port_i2c_mem_read(&i2c, 0x50U, 0x20U, 1U, rx, 2U, 100U), PORT_OK);
    TF_CHECK_MEM(rx, data, 2U);

    /* 直接注入从机寄存器内容 */
    port_sim_i2c_set_mem(0x30U, 0x05U, 0x5AU);
    memset(rx, 0, sizeof(rx));
    TF_CHECK_INT(port_i2c_mem_read(&i2c, 0x30U, 0x05U, 1U, rx, 1U, 100U), PORT_OK);
    TF_CHECK_INT(rx[0], 0x5AU);
    tf_end();

    tf_begin("sim/i2c 组合传输、设备检测与 DMA");
    port_sim_i2c_set_mem(0x40U, 0x01U, 0x77U);
    port_sim_i2c_set_mem(0x40U, 0x02U, 0x88U);
    reg_only[0] = 0x01U;
    memset(rx, 0, sizeof(rx));
    TF_CHECK_INT(port_i2c_master_transfer(&i2c, 0x40U, reg_only, 1U,
                                          rx, 2U, 100U), PORT_OK);
    expect[0] = 0x77U;
    expect[1] = 0x88U;
    TF_CHECK_MEM(rx, expect, 2U);

    TF_CHECK_INT(port_i2c_is_device_ready(&i2c, 0x50U, 3U, 10U), PORT_OK);
    TF_CHECK_INT(port_i2c_is_device_ready(&i2c, 0x80U, 3U, 10U), PORT_NOT_FOUND);

    data[0] = 0x30U;
    data[1] = 0xDEU;
    data[2] = 0xADU;
    TF_CHECK_INT(port_i2c_master_transmit_dma(&i2c, 0x50U, data, 3U), PORT_OK);
    reg_only[0] = 0x30U;
    TF_CHECK_INT(port_i2c_master_transmit_dma(&i2c, 0x50U, reg_only, 1U), PORT_OK);
    memset(rx, 0, sizeof(rx));
    TF_CHECK_INT(port_i2c_master_receive_dma(&i2c, 0x50U, rx, 2U), PORT_OK);
    TF_CHECK_INT(rx[0], 0xDEU);
    TF_CHECK_INT(rx[1], 0xADU);

    TF_CHECK_INT(port_i2c_bus_recover(&i2c), PORT_OK);
    TF_CHECK_INT(port_i2c_abort(&i2c), PORT_OK);
    tf_end();

    tf_begin("sim/i2c 非法参数与未初始化");
    data[0] = 0x00U;
    TF_CHECK_INT(port_i2c_master_transmit(&i2c, 0x100U, data, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2c_master_receive(&i2c, 0x100U, rx, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2c_mem_write(&i2c, 0x100U, 0U, 1U, data, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2c_mem_read(&i2c, 0x100U, 0U, 1U, rx, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2c_mem_write(&i2c, 0x50U, 0U, 3U, data, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2c_mem_read(&i2c, 0x50U, 0U, 0U, rx, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2c_master_transmit(&i2c, 0x50U, NULL, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2c_master_receive(&i2c, 0x50U, NULL, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2c_master_transfer(&i2c, 0x50U, NULL, 1U, rx, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2c_master_transfer(&i2c, 0x50U, data, 1U, NULL, 1U, 0U), PORT_INVALID_PARAM);

    memset(&idle, 0, sizeof(idle));
    TF_CHECK_INT(port_i2c_master_transmit(&idle, 0x50U, data, 1U, 0U), PORT_NOT_READY);
    TF_CHECK_INT(port_i2c_master_receive(&idle, 0x50U, rx, 1U, 0U), PORT_NOT_READY);
    TF_CHECK_INT(port_i2c_mem_write(&idle, 0x50U, 0U, 1U, data, 1U, 0U), PORT_NOT_READY);
    TF_CHECK_INT(port_i2c_mem_read(&idle, 0x50U, 0U, 1U, rx, 1U, 0U), PORT_NOT_READY);
    TF_CHECK_INT(port_i2c_is_device_ready(&idle, 0x50U, 1U, 0U), PORT_NOT_READY);

    TF_CHECK_INT(port_i2c_master_transmit(NULL, 0x50U, data, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2c_bus_recover(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2c_abort(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2c_get_state(NULL), PORT_STATE_RESET);

    TF_CHECK_INT(port_i2c_deinit(&i2c), PORT_OK);
    TF_CHECK_INT(port_i2c_master_transmit(&i2c, 0x50U, data, 1U, 0U), PORT_NOT_READY);
    tf_end();
}

static void test_i2s(void)
{
    port_i2s_handle_t i2s;
    port_i2s_config_t config;
    uint8_t tx[4] = { 1U, 2U, 3U, 4U };
    uint8_t rx[8];
    int i;

    port_sim_reset_state();
    memset(&i2s, 0, sizeof(i2s));
    memset(&config, 0, sizeof(config));
    config.mode = PORT_I2S_MODE_MASTER_TX;
    config.sample_rate = 48000U;
    config.data_size = PORT_I2S_DATA_16BIT;

    tf_begin("sim/i2s 收发、DMA 与中止");
    TF_CHECK_INT(port_i2s_init(NULL, &config), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2s_init(&i2s, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2s_init(&i2s, &config), PORT_OK);

    TF_CHECK_INT(port_i2s_transmit(&i2s, tx, sizeof(tx), 0U), PORT_OK);
    memset(rx, 0xFF, sizeof(rx));
    TF_CHECK_INT(port_i2s_receive(&i2s, rx, sizeof(rx), 0U), PORT_OK);
    TF_CHECK_MEM(rx, tx, sizeof(tx));
    for (i = (int)sizeof(tx); i < (int)sizeof(rx); i++) {
        TF_CHECK_INT(rx[i], 0U);
    }

    TF_CHECK_INT(port_i2s_transmit_dma(&i2s, tx, 2U), PORT_OK);
    memset(rx, 0, sizeof(rx));
    TF_CHECK_INT(port_i2s_receive_dma(&i2s, rx, 2U), PORT_OK);
    TF_CHECK_MEM(rx, tx, 2U);

    TF_CHECK_INT(port_i2s_transmit(&i2s, NULL, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2s_receive(&i2s, NULL, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2s_transmit(NULL, tx, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_i2s_receive(NULL, rx, 1U, 0U), PORT_INVALID_PARAM);

    TF_CHECK_INT(port_i2s_abort(&i2s), PORT_OK);
    TF_CHECK_INT(port_i2s_abort(NULL), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("sim/i2s 未初始化与反初始化");
    {
        port_i2s_handle_t idle;
        memset(&idle, 0, sizeof(idle));
        TF_CHECK_INT(port_i2s_transmit(&idle, tx, 1U, 0U), PORT_NOT_READY);
        TF_CHECK_INT(port_i2s_receive(&idle, rx, 1U, 0U), PORT_NOT_READY);
    }

    TF_CHECK_INT(port_i2s_deinit(&i2s), PORT_OK);
    TF_CHECK_INT(port_i2s_transmit(&i2s, tx, 1U, 0U), PORT_NOT_READY);
    TF_CHECK_INT(port_i2s_receive(&i2s, rx, 1U, 0U), PORT_NOT_READY);
    tf_end();
}

static void test_can(void)
{
    port_can_handle_t can;
    port_can_handle_t idle;
    port_can_config_t config;
    port_can_frame_t frame;
    port_can_frame_t out;
    port_can_filter_t filter;
    int i;
    port_status_t status;

    port_sim_reset_state();
    memset(&can, 0, sizeof(can));
    memset(&config, 0, sizeof(config));
    config.bitrate = 500000U;
    config.mode = PORT_CAN_MODE_LOOPBACK;

    tf_begin("sim/can 初始化、启停与发送");
    TF_CHECK_INT(port_can_init(NULL, &config), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_can_init(&can, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_can_init(&can, &config), PORT_OK);

    TF_CHECK_INT(port_can_start(&can), PORT_OK);
    TF_CHECK_INT(port_can_stop(&can), PORT_OK);
    TF_CHECK_INT(port_can_start(&can), PORT_OK);

    TF_CHECK_INT(port_can_transmit(NULL, &frame, 0U), PORT_INVALID_PARAM);
    memset(&frame, 0, sizeof(frame));
    frame.id = 0x123U;
    frame.dlc = 2U;
    frame.data[0] = 0x11U;
    frame.data[1] = 0x22U;
    TF_CHECK_INT(port_can_transmit(&can, NULL, 0U), PORT_INVALID_PARAM);

    can.callback = can_cb;
    can.user_data = &s_marker;
    s_can_rx_count = 0;
    s_can_user_data = NULL;
    TF_CHECK_INT(port_can_transmit(&can, &frame, 0U), PORT_OK);
    TF_CHECK_INT(s_can_rx_count, 1);
    TF_CHECK(s_can_user_data == &s_marker);
    tf_end();

    tf_begin("sim/can 注入接收与超时");
    port_sim_can_inject(&can, &frame);
    TF_CHECK_INT(s_can_rx_count, 2);

    memset(&out, 0, sizeof(out));
    TF_CHECK_INT(port_can_receive(&can, &out, 0U), PORT_OK);
    TF_CHECK_INT(out.id, 0x123U);
    TF_CHECK_INT(out.dlc, 2U);
    TF_CHECK_INT(out.data[0], 0x11U);
    TF_CHECK_INT(out.data[1], 0x22U);

    TF_CHECK_INT(port_can_receive(&can, &out, 0U), PORT_TIMEOUT);
    TF_CHECK_INT(port_can_receive(&can, NULL, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_can_receive(NULL, &out, 0U), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("sim/can 过滤器");
    memset(&filter, 0, sizeof(filter));
    filter.id = 0x123U;
    filter.mask = 0x7FFU;
    filter.id_type = PORT_CAN_ID_STANDARD;

    TF_CHECK_INT(port_can_add_filter(&can, &filter), PORT_OK);
    TF_CHECK_INT(port_can_clear_filters(&can), PORT_OK);

    for (i = 0; i < 16; i++) {
        TF_CHECK_INT(port_can_add_filter(&can, &filter), PORT_OK);
    }
    status = port_can_add_filter(&can, &filter);
    TF_CHECK_INT(status, PORT_NO_MEMORY);
    TF_CHECK_INT(port_can_clear_filters(&can), PORT_OK);

    TF_CHECK_INT(port_can_add_filter(&can, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_can_add_filter(NULL, &filter), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_can_clear_filters(NULL), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("sim/can 反初始化");
    TF_CHECK_INT(port_can_deinit(&can), PORT_OK);
    TF_CHECK_INT(port_can_transmit(&can, &frame, 0U), PORT_NOT_READY);
    TF_CHECK_INT(port_can_receive(&can, &out, 0U), PORT_NOT_READY);
    TF_CHECK_INT(port_can_start(&can), PORT_NOT_READY);
    TF_CHECK_INT(port_can_add_filter(&can, &filter), PORT_NOT_READY);

    memset(&idle, 0, sizeof(idle));
    TF_CHECK_INT(port_can_start(&idle), PORT_NOT_READY);
    TF_CHECK_INT(port_can_transmit(&idle, &frame, 0U), PORT_NOT_READY);
    TF_CHECK_INT(port_can_deinit(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_can_start(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_can_stop(NULL), PORT_INVALID_PARAM);
    tf_end();
}

static void test_dma(void)
{
    port_dma_handle_t dma;
    port_dma_handle_t idle;
    port_dma_handle_t periph;
    port_dma_config_t config;
    uint8_t src[4] = { 0x01U, 0x02U, 0x03U, 0x04U };
    uint8_t dst[4];

    port_sim_reset_state();
    memset(&dma, 0, sizeof(dma));
    memset(&config, 0, sizeof(config));
    config.direction = PORT_DMA_MEMORY_TO_MEMORY;
    config.src_width = PORT_DMA_WIDTH_8BIT;
    config.dst_width = PORT_DMA_WIDTH_8BIT;
    config.mode = PORT_DMA_MODE_NORMAL;

    tf_begin("sim/dma 内存到内存与回调");
    TF_CHECK_INT(port_dma_init(NULL, &config), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_dma_init(&dma, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_dma_init(&dma, &config), PORT_OK);

    TF_CHECK_INT(port_dma_get_remaining(&dma), 0U);
    TF_CHECK_INT(port_dma_get_remaining(NULL), 0U);

    dma.callback = dma_cb;
    dma.user_data = &s_marker;
    s_dma_complete_count = 0;
    memset(dst, 0, sizeof(dst));
    TF_CHECK_INT(port_dma_start(&dma, (uintptr_t)src, (uintptr_t)dst, sizeof(src)), PORT_OK);
    TF_CHECK_MEM(dst, src, sizeof(src));
    TF_CHECK_INT(s_dma_complete_count, 1);

    TF_CHECK_INT(port_dma_start(&dma, (uintptr_t)src, (uintptr_t)dst, 0U), PORT_OK);
    TF_CHECK_INT(port_dma_start(&dma, 0U, (uintptr_t)dst, 4U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_dma_start(&dma, (uintptr_t)src, 0U, 4U), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("sim/dma 状态控制与未初始化");
    TF_CHECK_INT(port_dma_suspend(&dma), PORT_OK);
    TF_CHECK_INT(dma.base.state, PORT_STATE_SUSPENDED);
    TF_CHECK_INT(port_dma_resume(&dma), PORT_OK);
    TF_CHECK_INT(dma.base.state, PORT_STATE_READY);
    TF_CHECK_INT(port_dma_stop(&dma), PORT_OK);
    TF_CHECK_INT(dma.base.state, PORT_STATE_READY);

    memset(&idle, 0, sizeof(idle));
    TF_CHECK_INT(port_dma_start(&idle, (uintptr_t)src, (uintptr_t)dst, sizeof(src)),
                 PORT_NOT_READY);
    TF_CHECK_INT(port_dma_stop(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_dma_suspend(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_dma_resume(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_dma_deinit(NULL), PORT_INVALID_PARAM);

    memset(&periph, 0, sizeof(periph));
    config.direction = PORT_DMA_MEMORY_TO_PERIPH;
    TF_CHECK_INT(port_dma_init(&periph, &config), PORT_OK);
    TF_CHECK_INT(port_dma_start(&periph, (uintptr_t)src, (uintptr_t)dst, 4U),
                 PORT_NOT_SUPPORTED);
    tf_end();

    tf_begin("sim/dma 反初始化");
    TF_CHECK_INT(port_dma_deinit(&dma), PORT_OK);
    TF_CHECK_INT(port_dma_start(&dma, (uintptr_t)src, (uintptr_t)dst, sizeof(src)),
                 PORT_NOT_READY);
    tf_end();
}

void ut_port_sim_bus(void)
{
    test_gpio();
    test_exti();
    test_uart();
    test_spi();
    test_i2c();
    test_i2s();
    test_can();
    test_dma();
}
