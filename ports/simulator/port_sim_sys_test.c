/*
 * ports/simulator/port_sim.c —— WDG / RTC / FLASH / CRC / RNG / PWR 单元测试。
 * 全部断言均以模拟实现的实际行为为准。
 */
#include "test.h"
#include "port.h"
#include "port_sim.h"

#include <string.h>

#define TEST_FLASH_SIZE     65536U
#define TEST_FLASH_ERASE    1024U

static int s_alarm_count;
static port_rtc_alarm_t s_alarm_last;
static void *s_alarm_user;

static void rtc_alarm_cb(port_rtc_alarm_t alarm, void *user_data)
{
    s_alarm_count++;
    s_alarm_last = alarm;
    s_alarm_user = user_data;
}

static void rtc_time_set(port_rtc_time_t *t, uint16_t year, uint8_t month, uint8_t day,
                         uint8_t hour, uint8_t minute, uint8_t second)
{
    t->year = year;
    t->month = month;
    t->day = day;
    t->hour = hour;
    t->minute = minute;
    t->second = second;
    t->weekday = 0U;
}

void ut_port_sim_sys(void)
{
    port_wdg_handle_t wdg;
    port_wdg_config_t wdg_cfg;
    port_rtc_time_t rtc_now;
    port_rtc_time_t rtc_get;
    port_rtc_alarm_config_t alarm_cfg;
    port_flash_handle_t flash;
    port_flash_info_t flash_info;
    port_crc_handle_t crc;
    port_crc_config_t crc_cfg;
    port_rng_handle_t rng;
    port_pwr_config_t pwr_cfg;
    port_pwr_status_t pwr_status;
    uint32_t timestamp0;
    uint32_t timestamp1;
    uint32_t result;
    uint32_t rng_a;
    uint32_t rng_b;
    uint8_t buffer[32];
    uint8_t readback[32];
    const char *crc_data = "123456789";
    int user_data = 0;
    size_t i;

    tf_begin("sys/WDG 初始化、刷新计数与停止");
    port_sim_reset_state();
    wdg_cfg.type = PORT_WDG_INDEPENDENT;
    wdg_cfg.timeout_ms = 1000U;
    wdg_cfg.window_ms = 0U;
    TF_CHECK_INT(port_wdg_init(NULL, &wdg_cfg), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_wdg_init(&wdg, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_wdg_init(&wdg, &wdg_cfg), PORT_OK);
    TF_CHECK_INT(port_wdg_start(&wdg), PORT_OK);
    TF_CHECK_INT(port_sim_wdg_refresh_count(), 0U);
    TF_CHECK_INT(port_wdg_refresh(&wdg), PORT_OK);
    TF_CHECK_INT(port_wdg_refresh(&wdg), PORT_OK);
    TF_CHECK_INT(port_sim_wdg_refresh_count(), 2U);
    TF_CHECK_INT(port_wdg_refresh(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_wdg_stop(&wdg), PORT_OK);
    TF_CHECK_INT(wdg.base.state, PORT_STATE_RESET);
    TF_CHECK_INT(port_wdg_start(NULL), PORT_INVALID_PARAM);
    memset(&wdg, 0, sizeof(wdg));
    TF_CHECK_INT(port_wdg_start(&wdg), PORT_NOT_READY);
    TF_CHECK_INT(port_wdg_refresh(&wdg), PORT_NOT_READY);
    TF_CHECK_INT(port_wdg_stop(&wdg), PORT_OK);
    tf_end();

    tf_begin("sys/WDG timeout=0 与复位标志");
    port_sim_reset_state();
    wdg_cfg.type = PORT_WDG_WINDOW;
    wdg_cfg.timeout_ms = 0U;      /* 实现不校验 timeout */
    wdg_cfg.window_ms = 10U;
    TF_CHECK_INT(port_wdg_init(&wdg, &wdg_cfg), PORT_OK);
    TF_CHECK_INT(port_wdg_start(&wdg), PORT_OK);
    TF_CHECK(port_wdg_is_reset() == false);
    port_wdg_clear_reset_flag();
    TF_CHECK(port_wdg_is_reset() == false);
    tf_end();

    tf_begin("sys/RTC 时间设置与读取往返");
    port_sim_reset_state();
    TF_CHECK_INT(port_rtc_init(), PORT_OK);
    rtc_time_set(&rtc_now, 2024U, 5U, 6U, 7U, 8U, 9U);
    TF_CHECK_INT(port_rtc_set_time(&rtc_now), PORT_OK);
    memset(&rtc_get, 0, sizeof(rtc_get));
    TF_CHECK_INT(port_rtc_get_time(&rtc_get), PORT_OK);
    TF_CHECK_INT(rtc_get.year, 2024U);
    TF_CHECK_INT(rtc_get.month, 5U);
    TF_CHECK_INT(rtc_get.day, 6U);
    TF_CHECK_INT(rtc_get.hour, 7U);
    TF_CHECK_INT(rtc_get.minute, 8U);
    TF_CHECK_INT(rtc_get.second, 9U);
    TF_CHECK_INT(port_rtc_set_time(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_rtc_get_time(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_rtc_deinit(), PORT_OK);
    tf_end();

    tf_begin("sys/RTC 非法时间参数");
    port_sim_reset_state();
    TF_CHECK_INT(port_rtc_init(), PORT_OK);
    rtc_time_set(&rtc_now, 2024U, 5U, 6U, 7U, 8U, 9U);
    rtc_now.month = 0U;
    TF_CHECK_INT(port_rtc_set_time(&rtc_now), PORT_INVALID_PARAM);
    rtc_now.month = 13U;
    TF_CHECK_INT(port_rtc_set_time(&rtc_now), PORT_INVALID_PARAM);
    rtc_time_set(&rtc_now, 2024U, 5U, 6U, 7U, 8U, 9U);
    rtc_now.day = 0U;
    TF_CHECK_INT(port_rtc_set_time(&rtc_now), PORT_INVALID_PARAM);
    rtc_now.day = 32U;
    TF_CHECK_INT(port_rtc_set_time(&rtc_now), PORT_INVALID_PARAM);
    rtc_time_set(&rtc_now, 2024U, 5U, 6U, 7U, 8U, 9U);
    rtc_now.hour = 24U;
    TF_CHECK_INT(port_rtc_set_time(&rtc_now), PORT_INVALID_PARAM);
    rtc_time_set(&rtc_now, 2024U, 5U, 6U, 7U, 8U, 9U);
    rtc_now.minute = 60U;
    TF_CHECK_INT(port_rtc_set_time(&rtc_now), PORT_INVALID_PARAM);
    rtc_time_set(&rtc_now, 2024U, 5U, 6U, 7U, 8U, 9U);
    rtc_now.second = 60U;
    TF_CHECK_INT(port_rtc_set_time(&rtc_now), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("sys/RTC 时间戳单调与一致性");
    port_sim_reset_state();
    TF_CHECK_INT(port_rtc_init(), PORT_OK);
    rtc_time_set(&rtc_now, 2000U, 1U, 1U, 0U, 0U, 0U);
    TF_CHECK_INT(port_rtc_set_time(&rtc_now), PORT_OK);
    TF_CHECK_INT(port_rtc_get_timestamp(&timestamp0), PORT_OK);
    TF_CHECK_INT(timestamp0, 946684800U);   /* 2000-01-01 00:00:00 UTC */
    rtc_now.day = 2U;
    TF_CHECK_INT(port_rtc_set_time(&rtc_now), PORT_OK);
    TF_CHECK_INT(port_rtc_get_timestamp(&timestamp1), PORT_OK);
    TF_CHECK_INT(timestamp1 - timestamp0, 86400U);
    TF_CHECK(timestamp1 > timestamp0);
    rtc_time_set(&rtc_now, 2024U, 5U, 6U, 7U, 8U, 9U);
    port_sim_time_set(&rtc_now);
    TF_CHECK_INT(port_rtc_get_timestamp(&timestamp0), PORT_OK);
    TF_CHECK_INT(port_rtc_set_time(&rtc_now), PORT_OK);
    TF_CHECK_INT(port_rtc_get_timestamp(&timestamp1), PORT_OK);
    TF_CHECK_INT(timestamp1, timestamp0);
    TF_CHECK_INT(port_rtc_get_timestamp(NULL), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("sys/RTC 闹钟设置、触发与关闭");
    port_sim_reset_state();
    TF_CHECK_INT(port_rtc_init(), PORT_OK);
    rtc_time_set(&rtc_now, 2024U, 5U, 6U, 7U, 8U, 9U);
    TF_CHECK_INT(port_rtc_set_time(&rtc_now), PORT_OK);
    alarm_cfg.time = rtc_now;
    alarm_cfg.callback = rtc_alarm_cb;
    alarm_cfg.user_data = &user_data;
    TF_CHECK_INT(port_rtc_set_alarm(PORT_RTC_ALARM_A, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_rtc_set_alarm(PORT_RTC_ALARM_A, &alarm_cfg), PORT_OK);
    s_alarm_count = 0;
    s_alarm_last = PORT_RTC_ALARM_B;
    s_alarm_user = NULL;
    TF_CHECK_INT(port_rtc_get_time(&rtc_get), PORT_OK);
    TF_CHECK_INT(s_alarm_count, 1);
    TF_CHECK_INT(s_alarm_last, PORT_RTC_ALARM_A);
    TF_CHECK(s_alarm_user == &user_data);
    TF_CHECK_INT(port_rtc_get_time(&rtc_get), PORT_OK);
    TF_CHECK_INT(s_alarm_count, 1);         /* 同一次闹钟只触发一次 */
    TF_CHECK_INT(port_rtc_disable_alarm(PORT_RTC_ALARM_A), PORT_OK);
    TF_CHECK_INT(port_rtc_disable_alarm(PORT_RTC_ALARM_B), PORT_OK);
    TF_CHECK_INT(port_rtc_disable_alarm((port_rtc_alarm_t)2), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("sys/FLASH 初始化、信息与读写擦除");
    port_sim_reset_state();
    TF_CHECK_INT(port_flash_init(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_flash_init(&flash), PORT_OK);
    TF_CHECK_INT(port_flash_get_info(&flash, &flash_info), PORT_OK);
    TF_CHECK_INT(flash_info.size, TEST_FLASH_SIZE);
    TF_CHECK_INT(flash_info.erase_size, TEST_FLASH_ERASE);
    TF_CHECK_INT(flash_info.start, 0U);
    memset(readback, 0, sizeof(readback));
    TF_CHECK_INT(port_flash_read(&flash, 0U, readback, 16U), PORT_OK);
    for (i = 0U; i < 16U; i++) {
        TF_CHECK_INT(readback[i], 0xFFU);
    }
    memcpy(buffer, "port-flash", 10U);
    TF_CHECK_INT(port_flash_write(&flash, 0x100U, buffer, 10U), PORT_OK);
    memset(readback, 0, sizeof(readback));
    TF_CHECK_INT(port_flash_read(&flash, 0x100U, readback, 10U), PORT_OK);
    TF_CHECK_MEM(readback, buffer, 10U);
    TF_CHECK_INT(port_flash_erase(&flash, 0x100U, TEST_FLASH_ERASE), PORT_OK);
    memset(readback, 0xAA, sizeof(readback));
    TF_CHECK_INT(port_flash_read(&flash, 0x100U, readback, 16U), PORT_OK);
    for (i = 0U; i < 16U; i++) {
        TF_CHECK_INT(readback[i], 0xFFU);
    }
    TF_CHECK_INT(port_flash_write(&flash, 0x100U, buffer, 10U), PORT_OK);
    TF_CHECK_INT(port_flash_lock(&flash), PORT_OK);
    TF_CHECK_INT(port_flash_unlock(&flash), PORT_OK);
    TF_CHECK_INT(port_flash_deinit(&flash), PORT_OK);
    TF_CHECK_INT(port_flash_read(&flash, 0U, readback, 1U), PORT_NOT_READY);
    tf_end();

    tf_begin("sys/FLASH 越界与非法参数");
    port_sim_reset_state();
    TF_CHECK_INT(port_flash_init(&flash), PORT_OK);
    TF_CHECK_INT(port_flash_get_info(&flash, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_flash_get_info(NULL, &flash_info), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_flash_read(&flash, TEST_FLASH_SIZE, readback, 1U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_flash_read(&flash, TEST_FLASH_SIZE - 1U, readback, 2U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_flash_write(&flash, TEST_FLASH_SIZE, buffer, 1U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_flash_erase(&flash, TEST_FLASH_SIZE, 1U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_flash_read(&flash, 0U, NULL, 1U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_flash_write(&flash, 0U, NULL, 1U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_flash_read(&flash, 0U, NULL, 0U), PORT_OK);
    TF_CHECK_INT(port_flash_write(&flash, 0U, NULL, 0U), PORT_OK);
    TF_CHECK_INT(port_flash_get_info(NULL, NULL), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("sys/CRC 8/16/32 已知向量与复位");
    port_sim_reset_state();
    memset(&crc_cfg, 0, sizeof(crc_cfg));
    crc_cfg.width = PORT_CRC_WIDTH_8;
    crc_cfg.polynomial = 0x07U;
    crc_cfg.initial_value = 0x00U;
    crc_cfg.xor_output = 0x00U;
    TF_CHECK_INT(port_crc_init(NULL, &crc_cfg), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_crc_init(&crc, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_crc_init(&crc, &crc_cfg), PORT_OK);
    TF_CHECK_INT(port_crc_calculate(&crc, crc_data, 9U, &result), PORT_OK);
    TF_CHECK_INT(result, 0xF4U);
    TF_CHECK_INT(port_crc_reset(&crc), PORT_OK);
    TF_CHECK_INT(port_crc_calculate(&crc, crc_data, 9U, &result), PORT_OK);
    TF_CHECK_INT(result, 0xF4U);

    crc_cfg.width = PORT_CRC_WIDTH_16;
    crc_cfg.polynomial = 0x1021U;
    crc_cfg.initial_value = 0xFFFFU;
    TF_CHECK_INT(port_crc_init(&crc, &crc_cfg), PORT_OK);
    TF_CHECK_INT(port_crc_calculate(&crc, crc_data, 9U, &result), PORT_OK);
    TF_CHECK_INT(result, 0x29B1U);

    crc_cfg.width = PORT_CRC_WIDTH_32;
    crc_cfg.polynomial = 0x04C11DB7U;
    crc_cfg.initial_value = 0xFFFFFFFFU;
    crc_cfg.xor_output = 0xFFFFFFFFU;
    TF_CHECK_INT(port_crc_init(&crc, &crc_cfg), PORT_OK);
    TF_CHECK_INT(port_crc_calculate(&crc, crc_data, 9U, &result), PORT_OK);
    TF_CHECK_INT(result, 0xFC891918U);
    TF_CHECK_INT(port_crc_reset(&crc), PORT_OK);
    TF_CHECK_INT(port_crc_calculate(&crc, crc_data, 9U, &result), PORT_OK);
    TF_CHECK_INT(result, 0xFC891918U);

    TF_CHECK_INT(port_crc_calculate(&crc, crc_data, 9U, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_crc_calculate(&crc, NULL, 1U, &result), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_crc_calculate(&crc, NULL, 0U, &result), PORT_OK);
    TF_CHECK_INT(port_crc_calculate(NULL, crc_data, 9U, &result), PORT_INVALID_PARAM);
    crc_cfg.width = (port_crc_width_t)7;
    TF_CHECK_INT(port_crc_init(&crc, &crc_cfg), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_crc_reset(NULL), PORT_INVALID_PARAM);
    memset(&crc, 0, sizeof(crc));
    TF_CHECK_INT(port_crc_calculate(&crc, crc_data, 9U, &result), PORT_NOT_READY);
    TF_CHECK_INT(port_crc_reset(&crc), PORT_NOT_READY);
    TF_CHECK_INT(port_crc_deinit(NULL), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("sys/RNG 随机数与缓冲区填充");
    port_sim_reset_state();
    TF_CHECK_INT(port_rng_init(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_rng_init(&rng), PORT_OK);
    TF_CHECK_INT(port_rng_get(&rng, &rng_a), PORT_OK);
    TF_CHECK_INT(port_rng_get(&rng, &rng_b), PORT_OK);
    TF_CHECK(rng_a != rng_b);
    TF_CHECK_INT(port_rng_init(&rng), PORT_OK);
    memset(buffer, 0xAA, sizeof(buffer));
    TF_CHECK_INT(port_rng_generate(&rng, buffer, 4U), PORT_OK);
    TF_CHECK_INT(buffer[0], (uint8_t)(rng_a & 0xFFU));
    TF_CHECK_INT(buffer[1], (uint8_t)((rng_a >> 8) & 0xFFU));
    TF_CHECK_INT(buffer[2], (uint8_t)((rng_a >> 16) & 0xFFU));
    TF_CHECK_INT(buffer[3], (uint8_t)((rng_a >> 24) & 0xFFU));
    memset(buffer, 0xAA, sizeof(buffer));
    TF_CHECK_INT(port_rng_generate(&rng, buffer, sizeof(buffer)), PORT_OK);
    TF_CHECK(buffer[0] != 0xAAU || buffer[1] != 0xAAU || buffer[2] != 0xAAU ||
             buffer[3] != 0xAAU);
    TF_CHECK_INT(port_rng_generate(&rng, NULL, 1U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_rng_generate(&rng, NULL, 0U), PORT_OK);
    TF_CHECK_INT(port_rng_get(NULL, &rng_a), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_rng_get(&rng, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_rng_deinit(&rng), PORT_OK);
    TF_CHECK_INT(port_rng_get(&rng, &rng_a), PORT_NOT_READY);
    TF_CHECK_INT(port_rng_deinit(NULL), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("sys/PWR 模式进入、状态与标志清理");
    port_sim_reset_state();
    TF_CHECK_INT(port_pwr_init(), PORT_OK);
    memset(&pwr_status, 0xAA, sizeof(pwr_status));
    TF_CHECK_INT(port_pwr_get_status(&pwr_status), PORT_OK);
    TF_CHECK_INT(pwr_status.reset_flags, 0U);
    TF_CHECK_INT(pwr_status.wakeup_flags, 0U);
    pwr_cfg.mode = PORT_PWR_MODE_SLEEP;
    pwr_cfg.wakeup_sources = 0x5U;
    pwr_cfg.low_power_regulator = false;
    TF_CHECK_INT(port_pwr_enter(&pwr_cfg), PORT_OK);
    TF_CHECK_INT(port_pwr_get_status(&pwr_status), PORT_OK);
    TF_CHECK_INT(pwr_status.wakeup_flags, 0x5U);
    pwr_cfg.mode = PORT_PWR_MODE_STOP;
    pwr_cfg.wakeup_sources = 0x3U;
    TF_CHECK_INT(port_pwr_enter(&pwr_cfg), PORT_OK);
    TF_CHECK_INT(port_pwr_get_status(&pwr_status), PORT_OK);
    TF_CHECK_INT(pwr_status.wakeup_flags, 0x3U);
    pwr_cfg.mode = PORT_PWR_MODE_STANDBY;
    pwr_cfg.wakeup_sources = 0xFU;
    TF_CHECK_INT(port_pwr_enter(&pwr_cfg), PORT_OK);
    TF_CHECK_INT(port_pwr_get_status(&pwr_status), PORT_OK);
    TF_CHECK_INT(pwr_status.wakeup_flags, 0xFU);
    TF_CHECK_INT(port_pwr_clear_flags(), PORT_OK);
    TF_CHECK_INT(port_pwr_get_status(&pwr_status), PORT_OK);
    TF_CHECK_INT(pwr_status.wakeup_flags, 0U);
    TF_CHECK_INT(pwr_status.reset_flags, 0U);
    TF_CHECK_INT(port_pwr_enter(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_pwr_get_status(NULL), PORT_INVALID_PARAM);
    tf_end();
}
