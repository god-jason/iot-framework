/* motion_device.c 单元测试：设备注册与执行封装。 */
#include "test.h"
#include "motion_device.h"
#include "motion_error.h"

static int dev_gpio_value;
static int dev_busy;

static int dev_gpio_write(uint16_t id, uint8_t value)
{
    if (id == 9U) { return -1; }
    dev_gpio_value = (int)value;
    return 0;
}

static int dev_gpio_read(uint16_t id, uint8_t *value)
{
    if (id == 9U || value == NULL) { return -1; }
    *value = (uint8_t)dev_gpio_value;
    return 0;
}

static int dev_adc_read(uint16_t id, int32_t *value)
{
    (void)id;
    if (value == NULL) { return -1; }
    *value = 123;
    return 0;
}

static int dev_step_is_busy(uint16_t id)
{
    (void)id;
    return dev_busy;
}

void ut_motion_device(void)
{
    motion_device_ops_t ops;
    bool busy = false;
    uint8_t value = 0U;
    int32_t adc = 0;

    tf_begin("motion_device/注册与空实现");
    motion_device_reset();
    TF_CHECK(motion_device_get() == NULL);
    TF_CHECK_INT(motion_device_register(NULL), MOTION_ERR_PARAMETER);
    TF_CHECK_INT(motion_device_gpio_write(0U, 1U), MOTION_ERR_DEVICE);
    TF_CHECK_INT(motion_device_adc_read(0U, &adc), MOTION_ERR_DEVICE);
    tf_end();

    tf_begin("motion_device/执行封装");
    memset(&ops, 0, sizeof(ops));
    ops.gpio_write = dev_gpio_write;
    ops.gpio_read = dev_gpio_read;
    ops.adc_read = dev_adc_read;
    ops.step_is_busy = dev_step_is_busy;
    TF_CHECK_INT(motion_device_register(&ops), 0);
    {
        const motion_device_ops_t *registered = motion_device_get();

        TF_CHECK(registered != NULL);
        if (registered != NULL) {
            TF_CHECK(registered->gpio_write == dev_gpio_write);
            TF_CHECK(registered->step_is_busy == dev_step_is_busy);
        }
    }
    TF_CHECK_INT(motion_device_gpio_write(0U, 1U), 0);
    TF_CHECK_INT(motion_device_gpio_read(0U, &value), 0);
    TF_CHECK_INT(value, 1);
    TF_CHECK_INT(motion_device_adc_read(0U, &adc), 0);
    TF_CHECK_INT((int)adc, 123);
    dev_busy = 1;
    TF_CHECK_INT(motion_device_step_is_busy(0U, &busy), 0);
    TF_CHECK(busy);
    dev_busy = 0;
    TF_CHECK_INT(motion_device_step_is_busy(0U, &busy), 0);
    TF_CHECK(!busy);
    TF_CHECK_INT(motion_device_gpio_write(9U, 1U),
                 MOTION_ERR_DEVICE_EXEC);
    TF_CHECK_INT(motion_device_gpio_read(9U, &value),
                 MOTION_ERR_DEVICE_EXEC);
    tf_end();

    tf_begin("motion_device/占用记录与任意忙碌");
    motion_device_reset();
    memset(&ops, 0, sizeof(ops));
    ops.step_is_busy = dev_step_is_busy;
    ops.gpio_write = dev_gpio_write;
    ops.gpio_read = dev_gpio_read;
    motion_device_register(&ops);
    busy = true;
    TF_CHECK_INT(motion_device_any_busy(&busy), 0);
    TF_CHECK(!busy);
    dev_busy = 1;
    motion_device_note_start(MOTION_DEVICE_STEP, 0U);
    TF_CHECK_INT(motion_device_any_busy(&busy), 0);
    TF_CHECK(busy);
    motion_device_note_stop(MOTION_DEVICE_STEP, 0U);
    TF_CHECK_INT(motion_device_any_busy(&busy), 0);
    TF_CHECK(!busy);
    motion_device_note_start(MOTION_DEVICE_STEP, MOTION_MAX_DEVICES);
    TF_CHECK_INT(motion_device_any_busy(&busy), 0);
    TF_CHECK(!busy);
    TF_CHECK_INT(motion_device_any_busy(NULL), MOTION_ERR_PARAMETER);
    motion_device_reset();
    tf_end();
}
