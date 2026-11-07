/*
 * ports/simulator/port_sim.c —— ADC / DAC / COMP / OPAMP 单元测试。
 * 全部断言均以模拟实现的实际行为为准。
 */
#include "test_framework.h"
#include "port.h"
#include "port_sim.h"

#include <string.h>

static int s_evt_count;
static port_event_t s_evt_last;
static void *s_evt_user;

static void analog_cb(void *handle, port_event_t event, void *user_data)
{
    (void)handle;
    s_evt_count++;
    s_evt_last = event;
    s_evt_user = user_data;
}

static void adc_cfg_default(port_adc_config_t *cfg, port_adc_resolution_t resolution)
{
    cfg->resolution = resolution;
    cfg->alignment = PORT_ADC_ALIGN_RIGHT;
    cfg->trigger = PORT_ADC_TRIGGER_SOFTWARE;
    cfg->channel_count = 1U;
}

void ut_port_sim_analog(void)
{
    port_adc_handle_t adc;
    port_adc_config_t adc_cfg;
    port_adc_channel_config_t ch_cfg;
    port_dac_handle_t dac;
    port_dac_config_t dac_cfg;
    port_comp_handle_t comp;
    port_comp_config_t comp_cfg;
    port_opamp_handle_t opamp;
    port_opamp_config_t opamp_cfg;
    uint32_t value;
    uint32_t buffer[8];
    int user_data = 0;

    tf_begin("analog/ADC 初始化参数校验与通道配置");
    port_sim_reset_state();
    adc_cfg_default(&adc_cfg, PORT_ADC_RESOLUTION_12BIT);
    TF_CHECK_INT(port_adc_init(NULL, &adc_cfg), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_adc_init(&adc, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_adc_init(&adc, &adc_cfg), PORT_OK);
    TF_CHECK(adc.base.initialized);
    TF_CHECK_INT(adc.base.state, PORT_STATE_READY);
    TF_CHECK_INT(port_adc_config_channel(NULL, &ch_cfg), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_adc_config_channel(&adc, NULL), PORT_INVALID_PARAM);
    ch_cfg.channel = 0U;
    ch_cfg.rank = 1U;
    ch_cfg.sample_time = 7U;
    TF_CHECK_INT(port_adc_config_channel(&adc, &ch_cfg), PORT_OK);
    ch_cfg.channel = 31U;
    TF_CHECK_INT(port_adc_config_channel(&adc, &ch_cfg), PORT_OK);
    ch_cfg.channel = 32U;
    TF_CHECK_INT(port_adc_config_channel(&adc, &ch_cfg), PORT_INVALID_PARAM);
    memset(&adc, 0, sizeof(adc));
    TF_CHECK_INT(port_adc_config_channel(&adc, &ch_cfg), PORT_NOT_READY);
    tf_end();

    tf_begin("analog/ADC 启停、读取与量程钳制");
    port_sim_reset_state();
    adc_cfg_default(&adc_cfg, PORT_ADC_RESOLUTION_12BIT);
    TF_CHECK_INT(port_adc_init(&adc, &adc_cfg), PORT_OK);
    TF_CHECK_INT(port_adc_start(&adc), PORT_OK);
    port_sim_adc_set(3U, 1234U);
    TF_CHECK_INT(port_adc_read(&adc, 3U, &value, 10U), PORT_OK);
    TF_CHECK_INT(value, 1234U);
    port_sim_adc_set(5U, 5000U);
    TF_CHECK_INT(port_adc_read(&adc, 5U, &value, 0U), PORT_OK);
    TF_CHECK_INT(value, 4095U);
    port_sim_adc_set_max(6U, 100U);
    port_sim_adc_set(6U, 900U);
    TF_CHECK_INT(port_adc_read(&adc, 6U, &value, 0U), PORT_OK);
    TF_CHECK_INT(value, 100U);
    TF_CHECK_INT(port_adc_read(&adc, 32U, &value, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_adc_read(&adc, 0U, NULL, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_adc_read(NULL, 0U, &value, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_adc_stop(&adc), PORT_OK);
    tf_end();

    tf_begin("analog/ADC 分辨率与最大量程");
    port_sim_reset_state();
    adc_cfg_default(&adc_cfg, PORT_ADC_RESOLUTION_6BIT);
    TF_CHECK_INT(port_adc_init(&adc, &adc_cfg), PORT_OK);
    TF_CHECK_INT(port_adc_get_max_value(&adc), 63U);
    adc_cfg.resolution = PORT_ADC_RESOLUTION_8BIT;
    TF_CHECK_INT(port_adc_init(&adc, &adc_cfg), PORT_OK);
    TF_CHECK_INT(port_adc_get_max_value(&adc), 255U);
    adc_cfg.resolution = PORT_ADC_RESOLUTION_10BIT;
    TF_CHECK_INT(port_adc_init(&adc, &adc_cfg), PORT_OK);
    TF_CHECK_INT(port_adc_get_max_value(&adc), 1023U);
    adc_cfg.resolution = PORT_ADC_RESOLUTION_16BIT;
    TF_CHECK_INT(port_adc_init(&adc, &adc_cfg), PORT_OK);
    TF_CHECK_INT(port_adc_get_max_value(&adc), 65535U);
    TF_CHECK_INT(port_adc_get_max_value(NULL), 0U);
    memset(&adc, 0, sizeof(adc));
    TF_CHECK_INT(port_adc_get_max_value(&adc), 0U);
    tf_end();

    tf_begin("analog/ADC 中断、DMA 与校准");
    port_sim_reset_state();
    adc_cfg_default(&adc_cfg, PORT_ADC_RESOLUTION_12BIT);
    TF_CHECK_INT(port_adc_init(&adc, &adc_cfg), PORT_OK);
    s_evt_count = 0;
    s_evt_last = PORT_EVENT_ERROR;
    s_evt_user = NULL;
    adc.callback = analog_cb;
    adc.user_data = &user_data;
    TF_CHECK_INT(port_adc_start_it(&adc), PORT_OK);
    TF_CHECK_INT(s_evt_count, 1);
    TF_CHECK_INT(s_evt_last, PORT_EVENT_COMPLETE);
    TF_CHECK(s_evt_user == &user_data);
    port_sim_adc_set(0U, 11U);
    port_sim_adc_set(1U, 22U);
    port_sim_adc_set(2U, 33U);
    port_sim_adc_set(3U, 44U);
    memset(buffer, 0, sizeof(buffer));
    TF_CHECK_INT(port_adc_start_dma(&adc, buffer, 4U), PORT_OK);
    TF_CHECK_INT(buffer[0], 11U);
    TF_CHECK_INT(buffer[1], 22U);
    TF_CHECK_INT(buffer[2], 33U);
    TF_CHECK_INT(buffer[3], 44U);
    TF_CHECK_INT(s_evt_count, 2);
    TF_CHECK_INT(port_adc_start_dma(&adc, NULL, 1U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_adc_start_dma(&adc, NULL, 0U), PORT_OK);
    TF_CHECK_INT(port_adc_calibrate(&adc), PORT_OK);
    TF_CHECK_INT(port_adc_start_it(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_adc_calibrate(NULL), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("analog/ADC 未初始化状态");
    port_sim_reset_state();
    memset(&adc, 0, sizeof(adc));
    port_sim_adc_set(0U, 10U);
    TF_CHECK_INT(port_adc_read(&adc, 0U, &value, 0U), PORT_NOT_READY);
    TF_CHECK_INT(port_adc_start(&adc), PORT_NOT_READY);
    TF_CHECK_INT(port_adc_start_it(&adc), PORT_NOT_READY);
    TF_CHECK_INT(port_adc_start_dma(&adc, buffer, 1U), PORT_NOT_READY);
    TF_CHECK_INT(port_adc_calibrate(&adc), PORT_NOT_READY);
    TF_CHECK_INT(port_adc_deinit(NULL), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("analog/DAC 初始化、写入与 DMA");
    port_sim_reset_state();
    dac_cfg.channel = 1U;
    dac_cfg.alignment = PORT_DAC_ALIGN_12BIT_RIGHT;
    TF_CHECK_INT(port_dac_init(NULL, &dac_cfg), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_dac_init(&dac, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_dac_init(&dac, &dac_cfg), PORT_OK);
    TF_CHECK_INT(port_dac_start(&dac), PORT_OK);
    TF_CHECK_INT(port_dac_write(&dac, 1000U), PORT_OK);
    TF_CHECK_INT(port_dac_write(&dac, 5000U), PORT_OK);   /* 实现钳制到 4095 */
    buffer[0] = 0U;
    buffer[1] = 2048U;
    buffer[2] = 4095U;
    TF_CHECK_INT(port_dac_write_dma(&dac, buffer, 3U), PORT_OK);
    TF_CHECK_INT(port_dac_write_dma(&dac, NULL, 1U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_dac_write_dma(&dac, NULL, 0U), PORT_OK);
    TF_CHECK_INT(port_dac_write(NULL, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_dac_stop(&dac), PORT_OK);
    TF_CHECK_INT(port_dac_deinit(&dac), PORT_OK);
    TF_CHECK(!dac.base.initialized);
    TF_CHECK_INT(port_dac_write(&dac, 1U), PORT_NOT_READY);
    TF_CHECK_INT(port_dac_start(&dac), PORT_NOT_READY);
    tf_end();

    tf_begin("analog/DAC 非法通道按实现回落到 0");
    port_sim_reset_state();
    dac_cfg.channel = 9U;   /* 越界通道由实现映射到通道 0 */
    dac_cfg.alignment = PORT_DAC_ALIGN_8BIT;
    TF_CHECK_INT(port_dac_init(&dac, &dac_cfg), PORT_OK);
    TF_CHECK_INT(port_dac_write(&dac, 123U), PORT_OK);
    memset(&dac, 0, sizeof(dac));
    TF_CHECK_INT(port_dac_write(&dac, 1U), PORT_NOT_READY);
    TF_CHECK_INT(port_dac_deinit(NULL), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("analog/COMP 初始化、读取与启停");
    port_sim_reset_state();
    comp_cfg.positive_input = 1U;
    comp_cfg.negative_input = 2U;
    comp_cfg.polarity = PORT_COMP_POLARITY_NORMAL;
    TF_CHECK_INT(port_comp_init(NULL, &comp_cfg), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_comp_init(&comp, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_comp_init(&comp, &comp_cfg), PORT_OK);
    port_sim_comp_set(&comp, true);
    TF_CHECK_INT(port_comp_read(&comp, &(bool){ false }), PORT_OK);
    {
        bool level = false;
        TF_CHECK_INT(port_comp_read(&comp, &level), PORT_OK);
        TF_CHECK(level == true);
        port_sim_comp_set(&comp, false);
        TF_CHECK_INT(port_comp_read(&comp, &level), PORT_OK);
        TF_CHECK(level == false);
    }
    TF_CHECK_INT(port_comp_read(&comp, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_comp_read(NULL, &(bool){ false }), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_comp_start(&comp), PORT_OK);
    TF_CHECK_INT(port_comp_stop(&comp), PORT_OK);
    TF_CHECK_INT(port_comp_start(NULL), PORT_INVALID_PARAM);
    memset(&comp, 0, sizeof(comp));
    TF_CHECK_INT(port_comp_read(&comp, &(bool){ false }), PORT_NOT_READY);
    TF_CHECK_INT(port_comp_start(&comp), PORT_NOT_READY);
    tf_end();

    tf_begin("analog/OPAMP 配置与重复初始化");
    port_sim_reset_state();
    opamp_cfg.mode = PORT_OPAMP_MODE_PGA;
    opamp_cfg.gain = 4U;
    opamp_cfg.positive_input = 1U;
    opamp_cfg.negative_input = 2U;
    TF_CHECK_INT(port_opamp_init(NULL, &opamp_cfg), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_opamp_init(&opamp, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_opamp_init(&opamp, &opamp_cfg), PORT_OK);
    TF_CHECK_INT(opamp.config.gain, 4U);
    TF_CHECK_INT(opamp.config.mode, PORT_OPAMP_MODE_PGA);
    TF_CHECK_INT(port_opamp_start(&opamp), PORT_OK);
    TF_CHECK_INT(port_opamp_stop(&opamp), PORT_OK);
    opamp_cfg.mode = PORT_OPAMP_MODE_FOLLOWER;
    opamp_cfg.gain = 1U;
    TF_CHECK_INT(port_opamp_init(&opamp, &opamp_cfg), PORT_OK);
    TF_CHECK_INT(opamp.config.gain, 1U);
    TF_CHECK_INT(port_opamp_start(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_opamp_stop(NULL), PORT_INVALID_PARAM);
    memset(&opamp, 0, sizeof(opamp));
    TF_CHECK_INT(port_opamp_start(&opamp), PORT_NOT_READY);
    tf_end();
}
