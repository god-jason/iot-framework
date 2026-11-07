/* PORT 核心接口单元测试（主机模拟平台）。
 * 覆盖：port_init/port_deinit、芯片信息、tick 与延时、能力查询边界。 */
#include "test_framework.h"
#include "port_sim.h"

#include <string.h>

void ut_port_core(void)
{
    port_mcu_info_t info;
    uint32_t t0;
    uint32_t t1;
    uint32_t caps;

    tf_begin("port/core 初始化与芯片信息");
    port_sim_reset_state();
    TF_CHECK_INT(port_init(), PORT_OK);
    /* 可重复调用 */
    TF_CHECK_INT(port_init(), PORT_OK);

    memset(&info, 0, sizeof(info));
    TF_CHECK_INT(port_get_info(&info), PORT_OK);
    TF_CHECK_INT(info.family, PORT_MCU_SIMULATOR);
    TF_CHECK(info.core_clock > 0U);
    TF_CHECK(info.flash_size > 0U);
    TF_CHECK(info.sram_size > 0U);
    TF_CHECK_INT(port_get_info(NULL), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("port/core tick 推进与延时");
    port_sim_reset_state();
    t0 = port_tick_get();
    t1 = port_tick_get();
    TF_CHECK(t1 >= t0);

    port_delay_ms(50U);
    t1 = port_tick_get();
    TF_CHECK_INT(t1 - t0, 50U);

    t0 = port_tick_get();
    port_delay_us(0U);
    TF_CHECK_INT(port_tick_get(), t0);

    /* 模拟实现按向上取整推进毫秒 */
    port_delay_us(500U);
    TF_CHECK_INT(port_tick_get(), t0 + 1U);
    port_delay_us(1000U);
    TF_CHECK_INT(port_tick_get(), t0 + 2U);
    tf_end();

    tf_begin("port/core 能力查询");
    /* GPIO 在模拟平台不提供 PORT_CAP_* 位 */
    TF_CHECK_INT(port_get_capabilities(PORT_DEVICE_GPIO, 0U), 0U);

    caps = port_get_capabilities(PORT_DEVICE_UART, 0U);
    TF_CHECK_INT(caps, PORT_CAP_DMA | PORT_CAP_IT | PORT_CAP_IDLE_DETECT);

    caps = port_get_capabilities(PORT_DEVICE_SPI, 0U);
    TF_CHECK_INT(caps, PORT_CAP_DMA | PORT_CAP_IT);

    caps = port_get_capabilities(PORT_DEVICE_I2C, 0U);
    TF_CHECK_INT(caps, PORT_CAP_DMA | PORT_CAP_IT);

    TF_CHECK_INT(port_get_capabilities(PORT_DEVICE_CAN, 0U), PORT_CAP_IT);

    caps = port_get_capabilities(PORT_DEVICE_ADC, 0U);
    TF_CHECK_INT(caps, PORT_CAP_DMA | PORT_CAP_IT);

    TF_CHECK_INT(port_get_capabilities(PORT_DEVICE_DAC, 0U), PORT_CAP_DMA);
    TF_CHECK_INT(port_get_capabilities(PORT_DEVICE_PWM, 0U), 0U);
    TF_CHECK_INT(port_get_capabilities(PORT_DEVICE_TIMER, 0U), PORT_CAP_IT);
    TF_CHECK_INT(port_get_capabilities(PORT_DEVICE_FLASH, 0U), 0U);
    TF_CHECK_INT(port_get_capabilities(PORT_DEVICE_USB, 0U), 0U);

    caps = port_get_capabilities(PORT_DEVICE_ETH, 0U);
    TF_CHECK_INT(caps, PORT_CAP_DMA | PORT_CAP_IT);

    /* 实例号边界：合法 0..7，越界返回 0 */
    TF_CHECK(port_get_capabilities(PORT_DEVICE_UART, 7U) != 0U);
    TF_CHECK_INT(port_get_capabilities(PORT_DEVICE_UART, 8U), 0U);
    TF_CHECK_INT(port_get_capabilities(PORT_DEVICE_SPI, 255U), 0U);
    TF_CHECK_INT(port_get_capabilities((port_device_type_t)999, 0U), 0U);
    tf_end();

    tf_begin("port/core deinit 后可恢复");
    TF_CHECK_INT(port_deinit(), PORT_OK);
    TF_CHECK_INT(port_init(), PORT_OK);
    memset(&info, 0, sizeof(info));
    TF_CHECK_INT(port_get_info(&info), PORT_OK);
    TF_CHECK_INT(info.family, PORT_MCU_SIMULATOR);
    TF_CHECK(info.core_clock > 0U);
    tf_end();
}
