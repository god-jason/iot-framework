/*
 * board_rtc.c - STM32F103CBT6 板级 RTC 后端
 *
 * 首选：LSE 32.768kHz 低速晶振 + 片上 RTC 计数器。
 *   - RTC 预分频 32767，计数器 1Hz 递增，直接作为 Unix 秒；
 *   - 计数器位于后备域，复位后继续走（无 VBAT 时掉电丢失）；
 *   - 后备域首次配置时，计数器初始化为 ROS_RTC_DEFAULT_UNIX
 *     （2026-11-22 00:00:00 UTC），之后由 LSE 持续计时。
 *
 * 回退：LSE 未起振（板上未焊晶振等）时，使用内核 tick 分频的 1Hz
 *   软件计时，基准同样取 ROS_RTC_DEFAULT_UNIX。
 */

#include "board.h"
#include "ros.h"
#include "stm32.h"

#include "stm32f10x.h"

#define BOARD_RTC_LSE_TIMEOUT_MS  2000U
#define BOARD_RTC_PRL_1HZ         32767U

/* ---------------------------- 硬件 RTC（LSE） ---------------------------- */

/* 等待 RTC_CRL 的指定标志，超时返回 false */
static bool rtc_wait_crl(uint16_t mask, uint32_t timeout_ms)
{
    uint32_t start = ros_tick_get();

    while ((RTC->CRL & mask) == 0U) {
        if ((ros_tick_get() - start) >= timeout_ms) {
            return false;
        }
    }
    return true;
}

/* 启动 LSE 并等待就绪；板上无 32.768kHz 晶振时返回 false */
static bool rtc_lse_start(void)
{
    uint32_t start;

    RCC->BDCR |= RCC_BDCR_LSEON;
    start = ros_tick_get();
    while ((RCC->BDCR & RCC_BDCR_LSERDY) == 0U) {
        if ((ros_tick_get() - start) >= BOARD_RTC_LSE_TIMEOUT_MS) {
            return false;
        }
    }
    return true;
}

/* 进入 RTC 配置模式（等待上一次操作完成） */
static bool rtc_enter_config(void)
{
    if (!rtc_wait_crl(RTC_CRL_RTOFF, 100U)) {
        return false;
    }
    RTC->CRL |= (uint16_t)RTC_CRL_CNF;
    return true;
}

/* 退出配置模式并等待写入完成 */
static void rtc_exit_config(void)
{
    RTC->CRL &= (uint16_t)~RTC_CRL_CNF;
    (void)rtc_wait_crl(RTC_CRL_RTOFF, 100U);
}

/*
 * 配置 LSE 硬件 RTC。
 * 后备域已有运行的 RTC 时直接沿用计数器（复位不清零）；
 * 否则重新选择 LSE、预分频 1Hz，并把计数器初始化为编译时刻。
 */
static bool rtc_hw_config(void)
{
    uint32_t bdcr = RCC->BDCR;
    uint32_t ts;

    if ((bdcr & RCC_BDCR_RTCEN) != 0U &&
        (bdcr & RCC_BDCR_RTCSEL) == RCC_BDCR_RTCSEL_LSE &&
        (bdcr & RCC_BDCR_LSERDY) != 0U) {
        return true;                    /* 后备域保持，计数器继续计时 */
    }

    if ((bdcr & RCC_BDCR_RTCEN) != 0U) {
        /* 已选其它时钟源或时钟失效：复位后备域后按 LSE 重新配置 */
        RCC->BDCR |= RCC_BDCR_BDRST;
        RCC->BDCR &= ~RCC_BDCR_BDRST;
    }

    if (!rtc_lse_start()) {
        return false;
    }

    RCC->BDCR = (RCC->BDCR & ~(RCC_BDCR_RTCSEL | RCC_BDCR_BDRST)) |
                RCC_BDCR_RTCSEL_LSE;
    RCC->BDCR |= RCC_BDCR_RTCEN;

    if (!rtc_wait_crl(RTC_CRL_RSF, 100U)) {
        return false;
    }
    if (!rtc_enter_config()) {
        return false;
    }
    RTC->PRLH = 0U;
    RTC->PRLL = (uint16_t)BOARD_RTC_PRL_1HZ;

    ts = (uint32_t)ROS_RTC_DEFAULT_UNIX;
    RTC->CNTH = (uint16_t)(ts >> 16);
    RTC->CNTL = (uint16_t)(ts & 0xFFFFU);
    rtc_exit_config();
    return true;
}

/* 读 RTC 计数器；跨秒边界时重读，避免高/低字撕裂 */
static int board_rtc_hw_get(uint32_t *timestamp)
{
    uint32_t hi;
    uint32_t lo;
    uint32_t hi2;

    if (timestamp == NULL) {
        return ROS_INVALID_PARAM;
    }
    if (!rtc_wait_crl(RTC_CRL_RSF, 100U)) {
        return ROS_NOT_READY;
    }

    do {
        hi = RTC->CNTH;
        lo = RTC->CNTL;
        hi2 = RTC->CNTH;
    } while (hi != hi2);

    *timestamp = (hi << 16) | (lo & 0xFFFFU);
    return ROS_OK;
}

static int board_rtc_hw_set(uint32_t timestamp)
{
    if (!rtc_enter_config()) {
        return ROS_NOT_READY;
    }
    RTC->CNTH = (uint16_t)(timestamp >> 16);
    RTC->CNTL = (uint16_t)(timestamp & 0xFFFFU);
    rtc_exit_config();
    return ROS_OK;
}

/* ---------------------------- 后端注册 ---------------------------- */

static const ros_rtc_ops_t s_hw_ops = {
    .name = "lse-rtc",
    .source = ROS_RTC_SOURCE_HW,
    .get = board_rtc_hw_get,
    .set = board_rtc_hw_set
};

void board_rtc_init(void)
{
    /* 使能电源/备份域时钟，并解除备份域写保护 */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
    PWR->CR |= PWR_CR_DBP;

    if (rtc_hw_config()) {
        (void)ros_rtc_register(&s_hw_ops);
        return;
    }

    /* LSE 不可用：退回平台共用的 1Hz 软件计时 */
    stm32_rtc_soft_init();
}
