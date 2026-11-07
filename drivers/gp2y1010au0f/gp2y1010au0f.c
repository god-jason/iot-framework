#include "gp2y1010au0f.h"
#include "gp2y1010au0f_register.h"
#include "log.h"

static int gp2y_check(const gp2y1010au0f_handle_t *handle)
{
    if (handle == NULL || handle->adc == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)PORT_NOT_READY;
    }
    return DRIVER_OK;
}

/* LED 为低电平点亮（模块内部 NPN 驱动） */
static int gp2y_led_on(const gp2y1010au0f_handle_t *handle)
{
    if (!handle->led_enabled) {
        return DRIVER_OK;
    }
    if (driver_gpio_write(handle->led_pin, PORT_GPIO_LOW) != DRIVER_OK) {
        LOG_E("gp2y1010", "led on failed");
        return (int)PORT_IO_ERROR;
    }
    driver_delay_us(GP2Y1010AU0F_LED_ON_DELAY_US);
    return DRIVER_OK;
}

static void gp2y_led_off(const gp2y1010au0f_handle_t *handle)
{
    if (!handle->led_enabled) {
        return;
    }
    driver_delay_us(GP2Y1010AU0F_LED_TAIL_DELAY_US);
    (void)driver_gpio_write(handle->led_pin, PORT_GPIO_HIGH);
    driver_delay_us(GP2Y1010AU0F_SAMPLE_PERIOD_US);
}

int gp2y1010au0f_init(gp2y1010au0f_handle_t *handle, port_adc_handle_t *adc, uint8_t channel)
{
    port_adc_channel_config_t cfg;
    uint32_t adc_max;

    if (handle == NULL || adc == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    cfg.channel = channel;
    cfg.rank = 1U;
    cfg.sample_time = 0U;   /* 0：由平台选择默认采样时间 */
    if (port_adc_config_channel(adc, &cfg) != PORT_OK) {
        LOG_E("gp2y1010", "adc channel %u config failed", (unsigned)channel);
        return (int)PORT_IO_ERROR;
    }

    adc_max = port_adc_get_max_value(adc);

    handle->adc = adc;
    handle->channel = channel;
    handle->vref_mv = GP2Y1010AU0F_DEFAULT_VREF_MV;
    handle->adc_max = (adc_max != 0U) ? adc_max : GP2Y1010AU0F_DEFAULT_ADC_MAX;
    handle->voff_mv = GP2Y1010AU0F_DEFAULT_VOFF_MV;
    handle->led_pin = PORT_PIN(0U, 0U);
    handle->led_enabled = false;
    handle->initialized = true;

    LOG_I("gp2y1010", "init ok, ch=%u, voff=%.0fmV",
          (unsigned)channel, (double)handle->voff_mv);
    return DRIVER_OK;
}

int gp2y1010au0f_deinit(gp2y1010au0f_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->led_enabled) {
        (void)driver_gpio_write(handle->led_pin, PORT_GPIO_HIGH);
    }
    handle->led_enabled = false;
    handle->adc = NULL;
    handle->initialized = false;
    return DRIVER_OK;
}

int gp2y1010au0f_read_raw(gp2y1010au0f_handle_t *handle, uint32_t *raw)
{
    int ret = gp2y_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (raw == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (port_adc_read(handle->adc, handle->channel, raw, GP2Y1010AU0F_ADC_TIMEOUT_MS) != PORT_OK) {
        LOG_E("gp2y1010", "adc read failed");
        return (int)PORT_IO_ERROR;
    }
    return DRIVER_OK;
}

int gp2y1010au0f_read(gp2y1010au0f_handle_t *handle, float *density_ug_m3)
{
    uint32_t raw = 0U;
    float mv;
    float density;
    int ret;

    if (density_ug_m3 == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = gp2y_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* LED 脉冲时序：点亮 -> 稳定 280µs -> 采样 -> 熄灭 -> 保持周期 */
    ret = gp2y_led_on(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }
    ret = gp2y1010au0f_read_raw(handle, &raw);
    gp2y_led_off(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    mv = (float)raw * handle->vref_mv / (float)handle->adc_max;
    density = (mv - handle->voff_mv) * GP2Y1010AU0F_UG_PER_MV;
    if (density < 0.0f) {
        density = 0.0f;   /* 低于基线：洁净空气/基线漂移 */
    }
    if (density > GP2Y1010AU0F_DENSITY_MAX_UG_M3) {
        LOG_W("gp2y1010", "density %.0fug/m3 over range", (double)density);
        density = GP2Y1010AU0F_DENSITY_MAX_UG_M3;
    }

    *density_ug_m3 = density;
    return DRIVER_OK;
}

int gp2y1010au0f_set_led(gp2y1010au0f_handle_t *handle, port_pin_t pin)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    /* 先置高（熄灭）再切输出，避免上电瞬间 LED 常亮 */
    if (driver_gpio_output(pin, PORT_GPIO_HIGH) != DRIVER_OK) {
        LOG_E("gp2y1010", "led gpio init failed");
        return (int)PORT_IO_ERROR;
    }
    handle->led_pin = pin;
    handle->led_enabled = true;
    return DRIVER_OK;
}

int gp2y1010au0f_set_voffset(gp2y1010au0f_handle_t *handle, float voff_mv)
{
    if (handle == NULL || voff_mv < 0.0f) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->voff_mv = voff_mv;
    return DRIVER_OK;
}

int gp2y1010au0f_set_reference(gp2y1010au0f_handle_t *handle, float vref_mv, uint32_t adc_max)
{
    if (handle == NULL || vref_mv <= 0.0f || adc_max == 0U) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->vref_mv = vref_mv;
    handle->adc_max = adc_max;
    return DRIVER_OK;
}
