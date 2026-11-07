/*
 * ports/simulator/port_sim.c —— TIM / PWM / CAPTURE / ENCODER 单元测试。
 * 全部断言均以模拟实现的实际行为为准。
 */
#include "test_framework.h"
#include "port.h"
#include "port_sim.h"

#include <string.h>

static int s_evt_count;
static port_event_t s_evt_last;
static void *s_evt_user;

static void timer_cb(void *handle, port_event_t event, void *user_data)
{
    (void)handle;
    s_evt_count++;
    s_evt_last = event;
    s_evt_user = user_data;
}

static void tim_cfg_default(port_tim_config_t *cfg)
{
    cfg->prescaler = 71U;
    cfg->period = 999U;
    cfg->counter_mode = PORT_TIM_COUNTER_UP;
    cfg->clock_division = 0U;
}

void ut_port_sim_timer(void)
{
    port_tim_handle_t tim;
    port_tim_config_t tim_cfg;
    port_pwm_handle_t pwm;
    port_pwm_config_t pwm_cfg;
    port_capture_handle_t cap;
    port_capture_config_t cap_cfg;
    port_encoder_handle_t enc;
    port_encoder_config_t enc_cfg;
    int user_data = 0;

    tf_begin("timer/TIM 初始化与启停");
    port_sim_reset_state();
    tim_cfg_default(&tim_cfg);
    TF_CHECK_INT(port_tim_init(NULL, &tim_cfg), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_tim_init(&tim, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_tim_init(&tim, &tim_cfg), PORT_OK);
    TF_CHECK(tim.base.initialized);
    TF_CHECK_INT(port_tim_start(&tim), PORT_OK);
    TF_CHECK_INT(tim.base.state, PORT_STATE_BUSY);
    TF_CHECK_INT(port_tim_stop(&tim), PORT_OK);
    TF_CHECK_INT(tim.base.state, PORT_STATE_READY);
    TF_CHECK_INT(port_tim_deinit(&tim), PORT_OK);
    TF_CHECK(!tim.base.initialized);
    TF_CHECK_INT(port_tim_start(&tim), PORT_NOT_READY);
    TF_CHECK_INT(port_tim_set_counter(&tim, 1U), PORT_NOT_READY);
    TF_CHECK_INT(port_tim_deinit(NULL), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("timer/TIM 计数、周期与预分频");
    port_sim_reset_state();
    tim_cfg_default(&tim_cfg);
    TF_CHECK_INT(port_tim_init(&tim, &tim_cfg), PORT_OK);
    TF_CHECK_INT(port_tim_get_counter(&tim), 0U);
    TF_CHECK_INT(port_tim_set_counter(&tim, 4242U), PORT_OK);
    TF_CHECK_INT(port_tim_get_counter(&tim), 4242U);
    TF_CHECK_INT(port_tim_set_period(&tim, 1234U), PORT_OK);
    TF_CHECK_INT(tim.config.period, 1234U);
    TF_CHECK_INT(port_tim_set_prescaler(&tim, 7U), PORT_OK);
    TF_CHECK_INT(tim.config.prescaler, 7U);
    TF_CHECK_INT(port_tim_set_period(NULL, 1U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_tim_set_prescaler(NULL, 1U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_tim_set_counter(NULL, 1U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_tim_get_counter(NULL), 0U);
    memset(&tim, 0, sizeof(tim));
    TF_CHECK_INT(port_tim_set_counter(&tim, 1U), PORT_NOT_READY);
    TF_CHECK_INT(port_tim_get_counter(&tim), 0U);
    tf_end();

    tf_begin("timer/TIM 中断回调");
    port_sim_reset_state();
    tim_cfg_default(&tim_cfg);
    TF_CHECK_INT(port_tim_init(&tim, &tim_cfg), PORT_OK);
    s_evt_count = 0;
    s_evt_last = PORT_EVENT_ERROR;
    s_evt_user = NULL;
    tim.callback = timer_cb;
    tim.user_data = &user_data;
    TF_CHECK_INT(port_tim_start_it(&tim), PORT_OK);
    TF_CHECK_INT(s_evt_count, 1);
    TF_CHECK_INT(s_evt_last, PORT_EVENT_COMPLETE);
    TF_CHECK(s_evt_user == &user_data);
    TF_CHECK_INT(tim.base.state, PORT_STATE_BUSY);
    TF_CHECK_INT(port_tim_stop_it(&tim), PORT_OK);
    TF_CHECK_INT(tim.base.state, PORT_STATE_READY);
    TF_CHECK_INT(port_tim_start_it(NULL), PORT_INVALID_PARAM);
    memset(&tim, 0, sizeof(tim));
    TF_CHECK_INT(port_tim_start_it(&tim), PORT_NOT_READY);
    tf_end();

    tf_begin("timer/PWM 初始化、频率、占空比与极性");
    port_sim_reset_state();
    pwm_cfg.frequency = 1000U;
    pwm_cfg.duty = 5000U;
    pwm_cfg.polarity = PORT_PWM_POLARITY_HIGH;
    TF_CHECK_INT(port_pwm_init(NULL, &pwm_cfg), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_pwm_init(&pwm, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_pwm_init(&pwm, &pwm_cfg), PORT_OK);
    TF_CHECK_INT(port_pwm_start(&pwm), PORT_OK);
    TF_CHECK_INT(port_pwm_set_frequency(&pwm, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_pwm_set_frequency(&pwm, 2000U), PORT_OK);
    TF_CHECK_INT(pwm.config.frequency, 2000U);
    TF_CHECK_INT(port_pwm_set_duty(&pwm, 0U), PORT_OK);
    TF_CHECK_INT(pwm.config.duty, 0U);
    TF_CHECK_INT(port_pwm_set_duty(&pwm, 10000U), PORT_OK);
    TF_CHECK_INT(pwm.config.duty, 10000U);
    TF_CHECK_INT(port_pwm_set_duty(&pwm, 10001U), PORT_INVALID_PARAM);
    TF_CHECK_INT(pwm.config.duty, 10000U);
    TF_CHECK_INT(port_pwm_set_polarity(&pwm, PORT_PWM_POLARITY_LOW), PORT_OK);
    TF_CHECK_INT(pwm.config.polarity, PORT_PWM_POLARITY_LOW);
    TF_CHECK_INT(port_pwm_set_frequency(NULL, 1U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_pwm_set_duty(NULL, 1U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_pwm_set_polarity(NULL, PORT_PWM_POLARITY_HIGH), PORT_INVALID_PARAM);
    pwm_cfg.duty = 10001U;
    TF_CHECK_INT(port_pwm_init(&pwm, &pwm_cfg), PORT_INVALID_PARAM);
    pwm_cfg.duty = 5000U;
    TF_CHECK_INT(port_pwm_stop(&pwm), PORT_OK);
    TF_CHECK_INT(port_pwm_deinit(&pwm), PORT_OK);
    TF_CHECK_INT(port_pwm_start(&pwm), PORT_NOT_READY);
    TF_CHECK_INT(port_pwm_deinit(NULL), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("timer/CAPTURE 初始化、取值与中断");
    port_sim_reset_state();
    cap_cfg.edge = PORT_CAPTURE_RISING;
    cap_cfg.prescaler = 0U;
    cap_cfg.filter = 0U;
    TF_CHECK_INT(port_capture_init(NULL, &cap_cfg), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_capture_init(&cap, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_capture_init(&cap, &cap_cfg), PORT_OK);
    port_sim_capture_set(&cap, 0x1234U);
    TF_CHECK_INT(port_capture_get_value(&cap), 0x1234U);
    TF_CHECK_INT(port_capture_start(&cap), PORT_OK);
    TF_CHECK_INT(cap.base.state, PORT_STATE_BUSY);
    TF_CHECK_INT(port_capture_stop(&cap), PORT_OK);
    TF_CHECK_INT(cap.base.state, PORT_STATE_READY);
    s_evt_count = 0;
    s_evt_last = PORT_EVENT_ERROR;
    cap.callback = timer_cb;
    cap.user_data = &user_data;
    TF_CHECK_INT(port_capture_start_it(&cap), PORT_OK);
    TF_CHECK_INT(s_evt_count, 1);
    TF_CHECK_INT(s_evt_last, PORT_EVENT_COMPLETE);
    TF_CHECK_INT(port_capture_start(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_capture_stop(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_capture_get_value(NULL), 0U);
    memset(&cap, 0, sizeof(cap));
    TF_CHECK_INT(port_capture_start(&cap), PORT_NOT_READY);
    TF_CHECK_INT(port_capture_start_it(&cap), PORT_NOT_READY);
    TF_CHECK_INT(port_capture_get_value(&cap), 0U);
    tf_end();

    tf_begin("timer/ENCODER 计数、置数与反向");
    port_sim_reset_state();
    enc_cfg.mode = PORT_ENCODER_MODE_TI12;
    enc_cfg.period = 65535U;
    enc_cfg.filter = 0U;
    enc_cfg.reverse = false;
    TF_CHECK_INT(port_encoder_init(NULL, &enc_cfg), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_encoder_init(&enc, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_encoder_init(&enc, &enc_cfg), PORT_OK);
    TF_CHECK_INT(port_encoder_start(&enc), PORT_OK);
    TF_CHECK_INT(enc.base.state, PORT_STATE_BUSY);
    port_sim_encoder_set(&enc, 500);
    TF_CHECK_INT(port_encoder_get_count(&enc), 500);
    TF_CHECK_INT(port_encoder_set_count(&enc, 100), PORT_OK);
    TF_CHECK_INT(port_encoder_get_count(&enc), 100);
    TF_CHECK_INT(port_encoder_set_count(&enc, -20), PORT_OK);
    TF_CHECK_INT(port_encoder_get_count(&enc), -20);
    TF_CHECK_INT(port_encoder_stop(&enc), PORT_OK);
    TF_CHECK_INT(enc.base.state, PORT_STATE_READY);
    port_sim_reset_state();
    enc_cfg.reverse = true;
    TF_CHECK_INT(port_encoder_init(&enc, &enc_cfg), PORT_OK);
    port_sim_encoder_set(&enc, 250);
    TF_CHECK_INT(port_encoder_get_count(&enc), -250);
    TF_CHECK_INT(port_encoder_set_count(&enc, 10), PORT_OK);
    TF_CHECK_INT(port_encoder_get_count(&enc), -10);
    TF_CHECK_INT(port_encoder_get_count(NULL), 0);
    TF_CHECK_INT(port_encoder_set_count(NULL, 1), PORT_INVALID_PARAM);
    memset(&enc, 0, sizeof(enc));
    TF_CHECK_INT(port_encoder_start(&enc), PORT_NOT_READY);
    TF_CHECK_INT(port_encoder_set_count(&enc, 1), PORT_NOT_READY);
    TF_CHECK_INT(port_encoder_get_count(&enc), 0);
    tf_end();
}
