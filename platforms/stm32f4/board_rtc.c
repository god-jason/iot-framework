/*
 * board_rtc.c - STM32F407VE 板级 RTC 后端（LSE 低速晶振）
 *
 * 使用备份域 RTC + 32.768kHz 低速晶振（LSE）计时：
 *   - LSE 起振成功：RTC 时钟源选 LSE，精度由晶振决定（典型 ±20ppm）；
 *   - LSE 不可用（未焊晶振/起振失败）：自动退回 LSI（内部约 32kHz RC），
 *     走时可用但精度较差；
 *   - 两者都失败：退回内核 tick 软件 RTC（stm32_rtc_soft_init）。
 *
 * 首次上电（备份域标记无效）用 ROS_RTC_DEFAULT_UNIX 初始化日历并写入
 * 标记；之后只要 VDD 不断，RTC 由备份域供电持续走时，复位/重新烧写
 * 都不会丢失时间。无 VBAT 电池时，完全断电后时间会回到默认基准。
 */

#include "board.h"
#include "stm32.h"
#include "ros.h"
#include "log.h"
#include "stm32_cube.h"
#include "hal_rtc.h"

#include <string.h>

#define RTC_BKP_MAGIC      0x32F2U   /* 标记：日历已按默认基准初始化 */
#define RTC_LSE_TIMEOUT_MS 3000U     /* LSE 起振等待上限 */

static bool s_hw_ready;

/* Unix 秒 -> 日历（Howard Hinnant civil_from_days 算法） */
static void unix_to_time(uint32_t unix_sec, hal_rtc_time_t *t)
{
    int64_t z = (int64_t)(unix_sec / 86400U) + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    uint32_t year = yoe + (uint32_t)era * 400U;
    uint32_t doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    uint32_t mp = (5U * doy + 2U) / 153U;
    uint32_t day = doy - (153U * mp + 2U) / 5U + 1U;
    uint32_t month = (mp < 10U) ? (mp + 3U) : (mp - 9U);
    uint32_t sod = unix_sec % 86400U;

    if (month <= 2U) {
        year++;
    }
    t->year = (uint16_t)year;
    t->month = (uint8_t)month;
    t->day = (uint8_t)day;
    t->hour = (uint8_t)(sod / 3600U);
    t->minute = (uint8_t)((sod / 60U) % 60U);
    t->second = (uint8_t)(sod % 60U);
    /* 1970-01-01 是周四；weekday 约定 1=周一 .. 7=周日 */
    t->weekday = (uint8_t)(((unix_sec / 86400U) + 3U) % 7U + 1U);
}

/* 配置 RTC 时钟源：优先 LSE，失败退回 LSI；成功返回 true */
static bool rtc_clock_config(void)
{
    RCC_OscInitTypeDef osc;
    RCC_PeriphCLKInitTypeDef pclk;

    /* 备份域写保护：先开 PWR 时钟再解锁 */
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    memset(&osc, 0, sizeof(osc));
    memset(&pclk, 0, sizeof(pclk));
    pclk.PeriphClockSelection = RCC_PERIPHCLK_RTC;

    /* 1. 尝试 32.768kHz 低速晶振 */
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc.LSEState = RCC_LSE_ON;
    osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) == CUBE_HAL_OK) {
        pclk.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
        if (HAL_RCCEx_PeriphCLKConfig(&pclk) == CUBE_HAL_OK) {
            __HAL_RCC_RTC_ENABLE();
            return true;
        }
    }

    /* 2. LSE 不可用：退回内部 LSI */
    memset(&osc, 0, sizeof(osc));
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSI;
    osc.LSIState = RCC_LSI_ON;
    osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != CUBE_HAL_OK) {
        return false;
    }
    pclk.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != CUBE_HAL_OK) {
        return false;
    }
    __HAL_RCC_RTC_ENABLE();
    return true;
}

static int hw_rtc_get(uint32_t *timestamp)
{
    if (!s_hw_ready || timestamp == NULL) {
        return ROS_ERROR;
    }
    return (hal_rtc_get_timestamp(timestamp) == HAL_OK) ? ROS_OK : ROS_ERROR;
}

static int hw_rtc_set(uint32_t timestamp)
{
    hal_rtc_time_t t;

    if (!s_hw_ready) {
        return ROS_ERROR;
    }
    memset(&t, 0, sizeof(t));
    unix_to_time(timestamp, &t);
    return (hal_rtc_set_time(&t) == HAL_OK) ? ROS_OK : ROS_ERROR;
}

static const ros_rtc_ops_t s_rtc_ops = {
    .name = "f407ve-rtc",
    .source = ROS_RTC_SOURCE_HW,
    .get = hw_rtc_get,
    .set = hw_rtc_set
};

void board_rtc_init(void)
{
    (void)RTC_LSE_TIMEOUT_MS;    /* 起振超时由 HAL_RCC_OscConfig 内部处理 */

    if (!rtc_clock_config()) {
        LOG_W("rtc", "LSE/LSI unavailable, fallback to soft tick");
        stm32_rtc_soft_init();
        return;
    }
    if (hal_rtc_init() != HAL_OK) {
        LOG_W("rtc", "hardware RTC init failed, fallback to soft tick");
        stm32_rtc_soft_init();
        return;
    }
    s_hw_ready = true;

    /* 首次使用或备份域被复位：写入默认时间并打标记 */
    if (RTC->BKP0R != RTC_BKP_MAGIC) {
        hal_rtc_time_t t;

        memset(&t, 0, sizeof(t));
        unix_to_time((uint32_t)ROS_RTC_DEFAULT_UNIX, &t);
        if (hal_rtc_set_time(&t) == HAL_OK) {
            RTC->BKP0R = RTC_BKP_MAGIC;
        }
    }

    (void)ros_rtc_register(&s_rtc_ops);

    /* 启动即从备份域 RTC 取时间（系统时间直接以它为源） */
    {
        uint32_t ts = 0U;

        if (hw_rtc_get(&ts) == ROS_OK) {
            LOG_I("rtc", "backup RTC time unix=%u", (unsigned)ts);
        }
    }
}
