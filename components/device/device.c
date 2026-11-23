/*
 * device.c - 通用设备命令（GPIO / PWM）
 *
 * 命令在 device_init() 中注册到 cmd 全局命令表，所有 Shell 实例共享。
 */

#include "device.h"
#include "roshell.h"

#include <stdio.h>

#define DEVICE_TAG "device"

/* ============================== 公共解析 ============================== */

/* 解析引脚：PA5 / pb12 / 0x0105（十进制或十六进制原始编码） */
static bool dev_parse_pin(const char *s, uint16_t *pin)
{
    uint32_t port;
    uint32_t index = 0U;
    const char *p = s;

    if (s == NULL || pin == NULL) {
        return false;
    }

    if ((p[0] == 'P' || p[0] == 'p') && p[1] != '\0') {
        char c = str_upper_char(p[1]);

        if (c < 'A' || c > 'Z') {
            return false;
        }
        port = (uint32_t)(c - 'A');
        p += 2;
    } else if (p[0] >= '0' && p[0] <= '9') {
        char *end = NULL;
        unsigned long v = strtoul(p, &end, 0);

        if (end == p || *end != '\0') {
            return false;
        }
        *pin = (uint16_t)v;
        return true;
    } else {
        return false;
    }

    if (*p == '\0') {
        return false;
    }
    while (*p != '\0') {
        if (*p < '0' || *p > '9') {
            return false;
        }
        index = index * 10U + (uint32_t)(*p - '0');
        p++;
    }
    *pin = HAL_PIN(port, index);
    return true;
}

static void dev_format_pin(uint16_t pin, char *buf, size_t cap)
{
    (void)str_printf(buf, (uint32_t)cap, "P%c%u",
                   (char)('A' + (char)HAL_PIN_GET_PORT(pin)),
                   (unsigned)HAL_PIN_GET_INDEX(pin));
}

static bool gpio_parse_level(const char *s, hal_gpio_level_e *level)
{
    if (str_icmp(s, "1") || str_icmp(s, "on") || str_icmp(s, "high")) {
        *level = HAL_GPIO_HIGH;
        return true;
    }
    if (str_icmp(s, "0") || str_icmp(s, "off") || str_icmp(s, "low")) {
        *level = HAL_GPIO_LOW;
        return true;
    }
    return false;
}

/* ============================== gpio ============================== */

static int gpio_apply(int rc, const char *what, uint16_t pin)
{
    char name[12];

    dev_format_pin(pin, name, sizeof(name));
    if (rc != HAL_OK) {
        ros_printf("ERR %d: gpio %s %s failed: %d\n", -1, what, name, rc);
        return -1;
    }
    ros_printf("gpio %s %s ok\n", what, name);
    return 0;
}

static int device_gpio_cmd(const rs_cmd_ctx_t *ctx)
{
    const char *action = NULL;
    const char *pin_s = NULL;
    const char *a1 = NULL;
    const char *a2 = NULL;
    uint16_t pin;
    int rc;

    if (!rs_args_get_str(ctx->parsed, 0U, &action) ||
        !rs_args_get_str(ctx->parsed, 1U, &pin_s) || action == NULL || pin_s == NULL ||
        !dev_parse_pin(pin_s, &pin)) {
        ros_puts("usage: gpio mode <pin> in|out [pull] | set <pin> <0|1> | "
                 "get <pin> | toggle <pin> | deinit <pin>");
        return -1;
    }
    (void)rs_args_get_str(ctx->parsed, 2U, &a1);
    (void)rs_args_get_str(ctx->parsed, 3U, &a2);

    if (str_icmp(action, "mode")) {
        if (a1 == NULL) {
            return -1;
        }
        if (str_icmp(a1, "out")) {
            hal_gpio_level_e level = HAL_GPIO_LOW;

            if (a2 != NULL && !gpio_parse_level(a2, &level)) {
                return -1;
            }
            rc = hal_gpio_config_output(pin, level);
        } else if (str_icmp(a1, "in")) {
            hal_gpio_pull_e pull = HAL_GPIO_NOPULL;

            if (a2 != NULL) {
                if (str_icmp(a2, "up") || str_icmp(a2, "pullup")) {
                    pull = HAL_GPIO_PULLUP;
                } else if (str_icmp(a2, "down") || str_icmp(a2, "pulldown")) {
                    pull = HAL_GPIO_PULLDOWN;
                } else if (!str_icmp(a2, "none") && !str_icmp(a2, "nopull")) {
                    return -1;
                }
            }
            rc = hal_gpio_config_input(pin, pull);
        } else {
            return -1;
        }
        return gpio_apply(rc, "mode", pin);
    }

    if (str_icmp(action, "set")) {
        hal_gpio_level_e level;

        if (a1 == NULL || !gpio_parse_level(a1, &level)) {
            return -1;
        }
        return gpio_apply(hal_gpio_write(pin, level), "set", pin);
    }

    if (str_icmp(action, "get")) {
        hal_gpio_level_e level = HAL_GPIO_LOW;
        char name[12];

        rc = hal_gpio_read(pin, &level);
        if (rc != HAL_OK) {
            ros_printf("ERR %d: gpio get failed: %d\n", -1, rc);
            return -1;
        }
        dev_format_pin(pin, name, sizeof(name));
        ros_printf("gpio %s = %u\n", name,
                   (unsigned)((level == HAL_GPIO_HIGH) ? 1U : 0U));
        return 0;
    }

    if (str_icmp(action, "toggle")) {
        return gpio_apply(hal_gpio_toggle(pin), "toggle", pin);
    }
    if (str_icmp(action, "deinit")) {
        return gpio_apply(hal_gpio_deinit(pin), "deinit", pin);
    }

    return -1;
}

static const rs_args_t s_gpio_args[] = {
    { "action", 0, RS_ARGS_TYPE_STR, true, true, NULL,
      "mode|set|get|toggle|deinit" },
    { "pin", 0, RS_ARGS_TYPE_STR, true, true, NULL, "PA5 / 0x0105" },
    { "arg1", 0, RS_ARGS_TYPE_STR, true, false, NULL, "in|out / level" },
    { "arg2", 0, RS_ARGS_TYPE_STR, true, false, NULL, "pull / initial level" }
};

static const rs_cmd_t s_gpio_def = {
    "gpio", "GPIO mode / read / write", NULL, NULL,
    s_gpio_args, 4U, device_gpio_cmd, NULL
};

/* ============================== pwm ============================== */

typedef struct {
    bool       used;
    hal_pwm_t *handle;
} device_pwm_slot_t;

static device_pwm_slot_t s_pwm[DEVICE_PWM_MAX];

int device_pwm_bind(hal_pwm_t *handle)
{
    uint32_t i;

    if (handle == NULL) {
        return -1;
    }
    for (i = 0U; i < DEVICE_PWM_MAX; i++) {
        if (!s_pwm[i].used) {
            s_pwm[i].used = true;
            s_pwm[i].handle = handle;
            return 0;
        }
    }
    return -1;
}

int device_pwm_unbind(uint32_t index)
{
    if (index >= DEVICE_PWM_MAX || !s_pwm[index].used) {
        return -1;
    }
    s_pwm[index].used = false;
    s_pwm[index].handle = NULL;
    return 0;
}

/* 仅支持编号（0..DEVICE_PWM_MAX-1） */
static device_pwm_slot_t *pwm_lookup(const char *arg)
{
    char *end = NULL;
    unsigned long idx;

    if (arg == NULL) {
        return NULL;
    }
    idx = strtoul(arg, &end, 10);
    if (end == arg || *end != '\0' || idx >= DEVICE_PWM_MAX ||
        !s_pwm[idx].used) {
        return NULL;
    }
    return &s_pwm[idx];
}

static const char *pwm_state_name(hal_state_e state)
{
    switch (state) {
    case HAL_STATE_RESET:      return "RESET";
    case HAL_STATE_READY:      return "READY";
    case HAL_STATE_BUSY:       return "BUSY";
    case HAL_STATE_BUSY_TX:    return "BUSY_TX";
    case HAL_STATE_BUSY_RX:    return "BUSY_RX";
    case HAL_STATE_BUSY_TX_RX: return "BUSY_TX_RX";
    case HAL_STATE_ERROR:      return "ERROR";
    case HAL_STATE_SUSPENDED:  return "SUSPENDED";
    default:                   return "?";
    }
}

/* 占空比：0~10000 原始值，或带 % 的百分比（支持小数：12.5%） */
static bool pwm_parse_duty(const char *s, uint32_t *duty)
{
    char num[24];
    size_t len;
    bool percent = false;
    float v;

    if (s == NULL || duty == NULL) {
        return false;
    }
    len = strlen(s);
    if (len == 0U || len >= sizeof(num)) {
        return false;
    }
    if (s[len - 1U] == '%') {
        percent = true;
        len--;
        if (len == 0U) {
            return false;
        }
    }
    (void)memcpy(num, s, len);
    num[len] = '\0';
    if (!str_parse_float(num, &v) || v < 0.0f) {
        return false;
    }
    if (percent) {
        v *= 100.0f;
    }
    if (v > 10000.0f) {
        v = 10000.0f;
    }
    *duty = (uint32_t)(v + 0.5f);
    return true;
}

static void pwm_print_info(uint32_t index)
{
    const hal_pwm_t *h = s_pwm[index].handle;

    ros_printf("[%u] freq=%u Hz duty=%u.%02u%% pol=%s state=%s\n",
               (unsigned)index, (unsigned)h->config.frequency,
               (unsigned)(h->config.duty / 100U),
               (unsigned)(h->config.duty % 100U),
               (h->config.polarity == HAL_PWM_POLARITY_LOW) ? "low" : "high",
               pwm_state_name(h->base.state));
}

/*
 * 首次真正使用（freq / duty / start）时才初始化硬件：
 * 板级注册只绑定句柄与默认配置，不占用定时器/引脚。
 * 返回 0 表示已就绪，-1 失败。
 */
static int pwm_ensure_ready(device_pwm_slot_t *slot)
{
    if (slot == NULL || slot->handle == NULL) {
        return -1;
    }
    if (slot->handle->base.state != HAL_STATE_RESET) {
        return 0;
    }
    if (slot->handle->config.frequency == 0U) {
        slot->handle->config.frequency = 1000U;     /* 兜底默认频率 */
    }
    return (hal_pwm_init(slot->handle, &slot->handle->config) == HAL_OK) ? 0 : -1;
}

static int device_pwm_cmd(const rs_cmd_ctx_t *ctx)
{
    const char *target = NULL;
    const char *action = NULL;
    const char *value = NULL;
    device_pwm_slot_t *slot;
    uint32_t index;
    int rc;

    if (!rs_args_get_str(ctx->parsed, 0U, &target) || target == NULL) {
        ros_puts("usage: pwm list | pwm <index> start|stop|freq|duty|info");
        return -1;
    }
    if (str_icmp(target, "list")) {
        uint32_t i;
        bool any = false;

        for (i = 0U; i < DEVICE_PWM_MAX; i++) {
            if (s_pwm[i].used) {
                any = true;
                pwm_print_info(i);
            }
        }
        if (!any) {
            ros_puts("(no pwm bound)");
        }
        return 0;
    }

    (void)rs_args_get_str(ctx->parsed, 1U, &action);
    (void)rs_args_get_str(ctx->parsed, 2U, &value);
    if (action == NULL) {
        return -1;
    }
    slot = pwm_lookup(target);
    if (slot == NULL) {
        ros_printf("ERR %d: pwm index %s invalid\n", -1, target);
        return -1;
    }
    index = (uint32_t)(slot - s_pwm);

    if (str_icmp(action, "start")) {
        if (pwm_ensure_ready(slot) != 0) {
            ros_printf("ERR %d: pwm %u init failed\n", -1, (unsigned)index);
            return -1;
        }
        rc = hal_pwm_start(slot->handle);
    } else if (str_icmp(action, "stop")) {
        if (slot->handle->base.state == HAL_STATE_RESET) {
            pwm_print_info(index);                  /* 未初始化：无需停止 */
            return 0;
        }
        rc = hal_pwm_stop(slot->handle);
    } else if (str_icmp(action, "info")) {
        pwm_print_info(index);
        return 0;
    } else if (str_icmp(action, "freq")) {
        uint32_t hz = 0U;

        if (value == NULL || !str_parse_uint(value, &hz) || hz == 0U) {
            return -1;
        }
        slot->handle->config.frequency = hz;         /* 首次 init 直接采用该频率 */
        if (pwm_ensure_ready(slot) != 0) {
            ros_printf("ERR %d: pwm %u init failed\n", -1, (unsigned)index);
            return -1;
        }
        rc = hal_pwm_set_frequency(slot->handle, hz);
        if (rc == HAL_OK) {
            /* 同一定时器的通道共用 ARR/PSC：同步同组通道的配置显示值 */
            uint32_t i;

            for (i = 0U; i < DEVICE_PWM_MAX; i++) {
                if (s_pwm[i].used && s_pwm[i].handle != NULL &&
                    s_pwm[i].handle->config.id ==
                        slot->handle->config.id) {
                    s_pwm[i].handle->config.frequency = hz;
                }
            }
        }
    } else if (str_icmp(action, "duty")) {
        uint32_t duty;

        if (value == NULL || !pwm_parse_duty(value, &duty)) {
            return -1;
        }
        slot->handle->config.duty = duty;            /* 首次 init 直接采用该占空比 */
        if (pwm_ensure_ready(slot) != 0) {
            ros_printf("ERR %d: pwm %u init failed\n", -1, (unsigned)index);
            return -1;
        }
        rc = hal_pwm_set_duty(slot->handle, duty);
    } else {
        return -1;
    }

    if (rc != HAL_OK) {
        ros_printf("ERR %d: pwm %u %s failed: %d\n", -1, (unsigned)index,
                   action, rc);
        return -1;
    }
    pwm_print_info(index);
    return 0;
}

static const rs_args_t s_pwm_args[] = {
    { "target", 0, RS_ARGS_TYPE_STR, true, true, NULL, "list / index" },
    { "action", 0, RS_ARGS_TYPE_STR, true, false, NULL,
      "start|stop|freq|duty|info" },
    { "value", 0, RS_ARGS_TYPE_STR, true, false, NULL, "hz / duty" }
};

static const rs_cmd_t s_pwm_def = {
    "pwm", "PWM channel control", NULL, NULL,
    s_pwm_args, 3U, device_pwm_cmd, NULL
};

/* ============================== 注册 ============================== */

int device_init(void)
{
    static bool ready;

    if (ready) {
        return 0;
    }
    ready = true;
    if (rs_cmd_register(&s_gpio_def) != 0) {
        return -1;
    }
    if (rs_cmd_register(&s_pwm_def) != 0) {
        return -1;
    }
    return 0;
}
