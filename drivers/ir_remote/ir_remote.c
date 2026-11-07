#include <string.h>
#include "ir_remote.h"
#include "ir_remote_register.h"
#include "log.h"

#define IR_CHECK(h) \
    do { if ((h) == NULL || !(h)->initialized) { return (int)PORT_INVALID_PARAM; } } while (0)

static void ir_mark(ir_remote_handle_t *h, uint32_t duration_us)
{
    uint32_t period = 1000U / h->carrier_khz;
    uint32_t half;
    uint32_t elapsed = 0U;
    if (period < 2U) { period = 2U; }
    half = period / 2U;
    port_irq_disable();
    while (elapsed + period <= duration_us) {
        (void)driver_gpio_write(h->tx_pin, PORT_GPIO_HIGH);
        driver_delay_us(half);
        (void)driver_gpio_write(h->tx_pin, PORT_GPIO_LOW);
        driver_delay_us(period - half);
        elapsed += period;
    }
    port_irq_enable();
}

static void ir_space(ir_remote_handle_t *h, uint32_t duration_us)
{
    (void)driver_gpio_write(h->tx_pin, PORT_GPIO_LOW);
    driver_delay_us(duration_us);
}

int ir_remote_set_carrier(ir_remote_handle_t *h, uint16_t carrier_khz)
{
    IR_CHECK(h);
    if (carrier_khz < 20U || carrier_khz > 60U) { return (int)PORT_INVALID_PARAM; }
    h->carrier_khz = carrier_khz;
    return DRIVER_OK;
}

static void ir_send_bit(ir_remote_handle_t *h, uint8_t bit)
{
    ir_mark(h, IR_NEC_BIT_MARK);
    ir_space(h, (bit != 0U) ? IR_NEC_ONE_SPACE : IR_NEC_ZERO_SPACE);
}

int ir_remote_send_nec(ir_remote_handle_t *h, uint8_t address, uint8_t command)
{
    uint8_t bytes[4];
    size_t i;
    size_t bit;
    IR_CHECK(h);
    if (h->tx_pin == 0U) { return (int)PORT_NOT_SUPPORTED; }
    bytes[0] = address;
    bytes[1] = (uint8_t)~address;
    bytes[2] = command;
    bytes[3] = (uint8_t)~command;
    ir_mark(h, IR_NEC_LEADER_MARK);
    ir_space(h, IR_NEC_LEADER_SPACE);
    for (i = 0U; i < 4U; i++) {
        for (bit = 0U; bit < 8U; bit++) {
            ir_send_bit(h, (uint8_t)((bytes[i] >> bit) & 0x01U));
        }
    }
    ir_mark(h, IR_NEC_STOP_MARK);
    (void)driver_gpio_write(h->tx_pin, PORT_GPIO_LOW);
    return DRIVER_OK;
}

int ir_remote_encode_nec(const ir_remote_frame_t *frame, uint16_t *timings,
                         size_t max_timings, size_t *count)
{
    uint8_t bytes[4];
    size_t n = 0U;
    size_t i;
    size_t bit;
    if (frame == NULL || timings == NULL || count == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (max_timings < IR_NEC_TIMINGS_MIN + 1U) { return (int)PORT_OVERFLOW; }
    bytes[0] = frame->address;
    bytes[1] = (uint8_t)~frame->address;
    bytes[2] = frame->command;
    bytes[3] = (uint8_t)~frame->command;
    timings[n++] = IR_NEC_LEADER_MARK;
    timings[n++] = IR_NEC_LEADER_SPACE;
    for (i = 0U; i < 4U; i++) {
        for (bit = 0U; bit < 8U; bit++) {
            timings[n++] = IR_NEC_BIT_MARK;
            timings[n++] = ((bytes[i] >> bit) & 0x01U) != 0U ? IR_NEC_ONE_SPACE
                                                             : IR_NEC_ZERO_SPACE;
        }
    }
    timings[n++] = IR_NEC_STOP_MARK;
    *count = n;
    return DRIVER_OK;
}

int ir_remote_decode_nec(const uint16_t *timings, size_t count, ir_remote_frame_t *frame)
{
    uint8_t bytes[4] = { 0U, 0U, 0U, 0U };
    size_t i;
    if (timings == NULL || frame == NULL) { return (int)PORT_INVALID_PARAM; }
    if (count < IR_NEC_TIMINGS_MIN) { return (int)PORT_INVALID_PARAM; }
    if (timings[0] < IR_NEC_LEADER_MARK_MIN || timings[0] > IR_NEC_LEADER_MARK_MAX ||
        timings[1] < IR_NEC_LEADER_SPACE_MIN || timings[1] > IR_NEC_LEADER_SPACE_MAX) {
        return (int)PORT_CRC_ERROR;
    }
    for (i = 0U; i < IR_NEC_BITS; i++) {
        uint16_t mark = timings[2U + i * 2U];
        uint16_t space = timings[3U + i * 2U];
        if (mark < IR_NEC_MARK_MIN || mark > IR_NEC_MARK_MAX) {
            return (int)PORT_CRC_ERROR;
        }
        if (space >= IR_NEC_SPACE_ONE_MIN && space <= IR_NEC_SPACE_ONE_MAX) {
            bytes[i / 8U] |= (uint8_t)(1U << (i % 8U));
        } else if (space < IR_NEC_SPACE_ZERO_MIN || space > IR_NEC_SPACE_ZERO_MAX) {
            return (int)PORT_CRC_ERROR;
        }
    }
    if ((bytes[2] ^ bytes[3]) != 0xFF) { return (int)PORT_CRC_ERROR; }
    frame->address = bytes[0];
    frame->command = bytes[2];
    frame->address_inverted = ((bytes[0] ^ bytes[1]) == 0xFF);
    return DRIVER_OK;
}

int ir_remote_recv(ir_remote_handle_t *h, uint16_t *timings, size_t max_timings,
                   size_t *count, uint32_t timeout_ms)
{
    size_t n = 0U;
    uint32_t start;
    int level;

    IR_CHECK(h);
    if (timings == NULL || max_timings == 0U) { return (int)PORT_INVALID_PARAM; }
    if (h->rx_pin == 0U) { return (int)PORT_NOT_SUPPORTED; }
    if (count != NULL) { *count = 0U; }

    level = driver_gpio_read(h->rx_pin);
    if (level < 0) { return (int)PORT_IO_ERROR; }
    start = driver_tick_ms();
    /* 等待引导 mark（接收头闲置为高，收到载波拉低） */
    while (level != 0) {
        level = driver_gpio_read(h->rx_pin);
        if (level < 0) { return (int)PORT_IO_ERROR; }
        if ((uint32_t)(driver_tick_ms() - start) > timeout_ms) {
            return (int)PORT_TIMEOUT;
        }
        driver_delay_us(IR_POLL_US);
    }
    /* 逐段测量电平持续时间，直到帧结束或缓冲满 */
    while (n < max_timings) {
        int current = level;
        uint32_t ticks = 0U;
        while (driver_gpio_read(h->rx_pin) == current) {
            driver_delay_us(IR_POLL_US);
            ticks++;
            if (ticks * IR_POLL_US > IR_MAX_SEGMENT_US) { break; }
        }
        timings[n++] = (uint16_t)(ticks * IR_POLL_US);
        level = driver_gpio_read(h->rx_pin);
        if (level < 0) { return (int)PORT_IO_ERROR; }
        if (timings[n - 1U] >= IR_IDLE_US && level != 0) { break; }
    }
    if (count != NULL) { *count = n; }
    return (n >= IR_NEC_TIMINGS_MIN) ? DRIVER_OK : (int)PORT_TIMEOUT;
}

int ir_remote_init(ir_remote_handle_t *handle, port_pin_t tx_pin, port_pin_t rx_pin)
{
    if (handle == NULL || (tx_pin == 0U && rx_pin == 0U)) {
        return (int)PORT_INVALID_PARAM;
    }
    (void)memset(handle, 0, sizeof(*handle));
    handle->tx_pin = tx_pin;
    handle->rx_pin = rx_pin;
    handle->carrier_khz = IR_NEC_CARRIER_KHZ_DEFAULT;
    handle->timeout_ms = 100U;
    if (tx_pin != 0U && driver_gpio_output(tx_pin, PORT_GPIO_LOW) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (rx_pin != 0U && driver_gpio_input(rx_pin, PORT_GPIO_PULLUP) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    handle->initialized = true;
    return DRIVER_OK;
}

int ir_remote_deinit(ir_remote_handle_t *handle)
{
    if (handle == NULL) { return (int)PORT_INVALID_PARAM; }
    if (handle->tx_pin != 0U) {
        (void)driver_gpio_write(handle->tx_pin, PORT_GPIO_LOW);
    }
    handle->initialized = false;
    return DRIVER_OK;
}
