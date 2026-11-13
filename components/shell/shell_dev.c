/*
 * shell_dev.c - 设备命令：gpio / pwm / stepper
 *
 * gpio    : 直接调用 hal_gpio_*，引脚写法支持 PA5 / pb12 / 0x0105。
 * pwm     : 只能操作由平台通过 shell_pwm_bind() 绑定过名字的通道，
 *           因为定时器句柄（base.instance / channel）必须由平台预先准备。
 * stepper : 以 GPIO 方向 + 脉冲为后端的步进电机，脉冲半周期由
 *           `stepper speed <us>` 配置；move 为阻塞执行。
 *
 * 本文件属于 shell 组件内部实现，命令在按需实例化时注册，
 * 内存归属落在 shell 任务名下。
 */

#include "shell_internal.h"
#include "shell_dev.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ============================== 公共解析 ============================== */

/* 解析引脚：PA5 / pb12 / 0x0105（十进制或十六进制原始编码） */
static bool dev_parse_pin(const char *s, hal_pin_t *pin)
{
    uint32_t port;
    uint32_t index = 0U;
    const char *p = s;

    if (s == NULL || pin == NULL) {
        return false;
    }

    if ((p[0] == 'P' || p[0] == 'p') && p[1] != '\0') {
        char c = p[1];

        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
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
        *pin = (hal_pin_t)v;
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

/* 引脚格式化为 PA5 形式 */
static void dev_format_pin(hal_pin_t pin, char *buf, size_t cap)
{
    (void)snprintf(buf, cap, "P%c%u",
                   (char)('A' + (char)HAL_PIN_GET_PORT(pin)),
                   (unsigned)HAL_PIN_GET_INDEX(pin));
}

/* 解析有符号 32 位整数（支持 0x 前缀） */
static bool dev_parse_i32(const char *s, int32_t *out)
{
    char *end = NULL;
    long v;

    if (s == NULL || out == NULL) {
        return false;
    }
    v = strtol(s, &end, 0);
    if (end == s || *end != '\0') {
        return false;
    }
    if (v < (long)INT32_MIN || v > (long)INT32_MAX) {
        return false;
    }
    *out = (int32_t)v;
    return true;
}

/* ============================== gpio ============================== */

#define GPIO_USAGE \
    "gpio mode <pin> in|out [pull] | set <pin> <0|1> | get <pin> | " \
    "toggle <pin> | deinit <pin>"

static int gpio_usage(shell_t *sh)
{
    (void)sh;
    ros_printf("usage: %s\n", GPIO_USAGE);
    return -SHELL_ERR_PARAM;
}

static bool gpio_parse_level(const char *s, hal_gpio_level_e *level)
{
    if (strcmp(s, "1") == 0 || strcmp(s, "on") == 0 ||
        strcmp(s, "high") == 0) {
        *level = HAL_GPIO_HIGH;
        return true;
    }
    if (strcmp(s, "0") == 0 || strcmp(s, "off") == 0 ||
        strcmp(s, "low") == 0) {
        *level = HAL_GPIO_LOW;
        return true;
    }
    return false;
}

static int gpio_apply(shell_t *sh, int rc, const char *what,
                      hal_pin_t pin)
{
    char name[12];

    dev_format_pin(pin, name, sizeof(name));
    if (rc != HAL_OK) {
        shell_error(sh, SHELL_ERR_DEVICE, "gpio %s %s failed: %d",
                    what, name, rc);
        return -SHELL_ERR_DEVICE;
    }
    ros_printf("gpio %s %s ok\n", what, name);
    return SHELL_OK;
}

static int cmd_gpio(shell_t *sh, int argc, char **argv)
{
    hal_pin_t pin;
    int rc;

    if (argc < 2) {
        return gpio_usage(sh);
    }

    if (strcmp(argv[1], "mode") == 0) {
        if (argc < 4 || !dev_parse_pin(argv[2], &pin)) {
            return gpio_usage(sh);
        }
        if (strcmp(argv[3], "out") == 0) {
            hal_gpio_level_e level = HAL_GPIO_LOW;

            if (argc >= 5 && !gpio_parse_level(argv[4], &level)) {
                return gpio_usage(sh);
            }
            rc = hal_gpio_config_output(pin, level);
        } else if (strcmp(argv[3], "in") == 0) {
            hal_gpio_pull_e pull = HAL_GPIO_NOPULL;

            if (argc >= 5) {
                if (strcmp(argv[4], "up") == 0 ||
                    strcmp(argv[4], "pullup") == 0) {
                    pull = HAL_GPIO_PULLUP;
                } else if (strcmp(argv[4], "down") == 0 ||
                           strcmp(argv[4], "pulldown") == 0) {
                    pull = HAL_GPIO_PULLDOWN;
                } else if (strcmp(argv[4], "none") != 0 &&
                           strcmp(argv[4], "nopull") != 0) {
                    return gpio_usage(sh);
                }
            }
            rc = hal_gpio_config_input(pin, pull);
        } else {
            return gpio_usage(sh);
        }
        return gpio_apply(sh, rc, "mode", pin);
    }

    if (strcmp(argv[1], "set") == 0) {
        hal_gpio_level_e level;

        if (argc < 4 || !dev_parse_pin(argv[2], &pin) ||
            !gpio_parse_level(argv[3], &level)) {
            return gpio_usage(sh);
        }
        return gpio_apply(sh, hal_gpio_write(pin, level), "set", pin);
    }

    if (strcmp(argv[1], "get") == 0) {
        hal_gpio_level_e level = HAL_GPIO_LOW;
        char name[12];

        if (argc < 3 || !dev_parse_pin(argv[2], &pin)) {
            return gpio_usage(sh);
        }
        rc = hal_gpio_read(pin, &level);
        if (rc != HAL_OK) {
            shell_error(sh, SHELL_ERR_DEVICE, "gpio get failed: %d", rc);
            return -SHELL_ERR_DEVICE;
        }
        dev_format_pin(pin, name, sizeof(name));
        ros_printf("gpio %s = %u\n", name,
                     (unsigned)((level == HAL_GPIO_HIGH) ? 1U : 0U));
        return SHELL_OK;
    }

    if (strcmp(argv[1], "toggle") == 0) {
        if (argc < 3 || !dev_parse_pin(argv[2], &pin)) {
            return gpio_usage(sh);
        }
        return gpio_apply(sh, hal_gpio_toggle(pin), "toggle", pin);
    }

    if (strcmp(argv[1], "deinit") == 0) {
        if (argc < 3 || !dev_parse_pin(argv[2], &pin)) {
            return gpio_usage(sh);
        }
        return gpio_apply(sh, hal_gpio_deinit(pin), "deinit", pin);
    }

    return gpio_usage(sh);
}

/* ============================== pwm ============================== */

typedef struct {
    bool       used;
    char       name[SHELL_PWM_NAME_MAX];
    hal_pwm_t *handle;
} shell_pwm_slot_t;

static shell_pwm_slot_t s_pwm[SHELL_PWM_MAX];

static shell_pwm_slot_t *pwm_find(const char *name)
{
    uint32_t i;

    for (i = 0U; i < SHELL_PWM_MAX; i++) {
        if (s_pwm[i].used && strcmp(s_pwm[i].name, name) == 0) {
            return &s_pwm[i];
        }
    }
    return NULL;
}

/* 支持两种寻址：绑定名（如 pwm1）或槽位序号（如 0） */
static shell_pwm_slot_t *pwm_lookup(const char *arg)
{
    shell_pwm_slot_t *slot = pwm_find(arg);
    char *end = NULL;
    unsigned long idx;

    if (slot != NULL) {
        return slot;
    }
    idx = strtoul(arg, &end, 10);
    if (end != arg && *end == '\0' && idx < SHELL_PWM_MAX &&
        s_pwm[idx].used) {
        return &s_pwm[idx];
    }
    return NULL;
}

int shell_pwm_bind(const char *name, hal_pwm_t *handle)
{
    size_t len;
    uint32_t i;

    if (name == NULL || handle == NULL) {
        return -1;
    }
    len = strlen(name);
    if (len == 0U || len >= SHELL_PWM_NAME_MAX) {
        return -1;
    }

    if (pwm_find(name) != NULL) {
        pwm_find(name)->handle = handle;    /* 同名重复绑定：更新句柄 */
        return 0;
    }
    for (i = 0U; i < SHELL_PWM_MAX; i++) {
        if (!s_pwm[i].used) {
            s_pwm[i].used = true;
            memcpy(s_pwm[i].name, name, len + 1U);
            s_pwm[i].handle = handle;
            return 0;
        }
    }
    return -1;
}

int shell_pwm_unbind(const char *name)
{
    shell_pwm_slot_t *slot;

    if (name == NULL) {
        return -1;
    }
    slot = pwm_find(name);
    if (slot == NULL) {
        return -1;
    }
    slot->used = false;
    slot->name[0] = '\0';
    slot->handle = NULL;
    return 0;
}

static const char *pwm_state_name(hal_state_e state)
{
    switch (state) {
    case HAL_STATE_RESET:     return "RESET";
    case HAL_STATE_READY:     return "READY";
    case HAL_STATE_BUSY:      return "BUSY";
    case HAL_STATE_BUSY_TX:   return "BUSY_TX";
    case HAL_STATE_BUSY_RX:   return "BUSY_RX";
    case HAL_STATE_BUSY_TX_RX: return "BUSY_TX_RX";
    case HAL_STATE_ERROR:     return "ERROR";
    case HAL_STATE_SUSPENDED: return "SUSPENDED";
    default:                  return "?";
    }
}

/* 占空比：0~10000 原始值，或带 % 的百分比（支持小数：12.5%） */
static bool pwm_parse_duty(const char *s, uint32_t *duty)
{
    char *end = NULL;
    double v;

    if (s == NULL || duty == NULL) {
        return false;
    }
    v = strtod(s, &end);
    if (end == s || v < 0.0) {
        return false;
    }
    if (*end == '%') {
        end++;
        v *= 100.0;
    }
    if (*end != '\0') {
        return false;
    }
    if (v > 10000.0) {
        v = 10000.0;
    }
    *duty = (uint32_t)(v + 0.5);
    return true;
}

static void pwm_print_info(shell_t *sh, const shell_pwm_slot_t *slot)
{
    const hal_pwm_t *h = slot->handle;

    (void)sh;
    ros_printf("%-6s freq=%u Hz duty=%u.%02u%% pol=%s state=%s\n",
                 slot->name, (unsigned)h->config.frequency,
                 (unsigned)(h->config.duty / 100U),
                 (unsigned)(h->config.duty % 100U),
                 (h->config.polarity == HAL_PWM_POLARITY_LOW) ? "low" : "high",
                 pwm_state_name(h->base.state));
}

#define PWM_USAGE \
    "pwm list | pwm <name|index> start | stop | freq <hz> | " \
    "duty <0-10000|n%> | info"

static int pwm_usage(shell_t *sh)
{
    (void)sh;
    ros_printf("usage: %s\n", PWM_USAGE);
    return -SHELL_ERR_PARAM;
}

static int cmd_pwm(shell_t *sh, int argc, char **argv)
{
    shell_pwm_slot_t *slot;
    int rc;

    if (argc < 2) {
        return pwm_usage(sh);
    }

    if (strcmp(argv[1], "list") == 0) {
        uint32_t i;
        bool any = false;

        for (i = 0U; i < SHELL_PWM_MAX; i++) {
            if (s_pwm[i].used) {
                any = true;
                ros_printf("[%u] ", (unsigned)i);
                pwm_print_info(sh, &s_pwm[i]);
            }
        }
        if (!any) {
            ros_puts("(no pwm bound)");
        }
        return SHELL_OK;
    }

    if (argc < 3) {
        return pwm_usage(sh);
    }
    slot = pwm_lookup(argv[1]);
    if (slot == NULL) {
        shell_error(sh, SHELL_ERR_NOT_FOUND, "pwm %s not bound", argv[1]);
        return -SHELL_ERR_NOT_FOUND;
    }

    if (strcmp(argv[2], "start") == 0) {
        rc = hal_pwm_start(slot->handle);
    } else if (strcmp(argv[2], "stop") == 0) {
        rc = hal_pwm_stop(slot->handle);
    } else if (strcmp(argv[2], "info") == 0) {
        pwm_print_info(sh, slot);
        return SHELL_OK;
    } else if (strcmp(argv[2], "freq") == 0) {
        uint32_t hz;

        if (argc < 4 || (hz = (uint32_t)strtoul(argv[3], NULL, 10)) == 0U) {
            return pwm_usage(sh);
        }
        rc = hal_pwm_set_frequency(slot->handle, hz);
    } else if (strcmp(argv[2], "duty") == 0) {
        uint32_t duty;

        if (argc < 4 || !pwm_parse_duty(argv[3], &duty)) {
            return pwm_usage(sh);
        }
        rc = hal_pwm_set_duty(slot->handle, duty);
    } else {
        return pwm_usage(sh);
    }

    if (rc != HAL_OK) {
        shell_error(sh, SHELL_ERR_DEVICE, "pwm %s %s failed: %d",
                    slot->name, argv[2], rc);
        return -SHELL_ERR_DEVICE;
    }
    pwm_print_info(sh, slot);
    return SHELL_OK;
}

/* ============================== fan ============================== */

static fan_t *s_fan[SHELL_FAN_MAX];

int shell_fan_bind(uint8_t id, fan_t *fan)
{
    if (id >= SHELL_FAN_MAX || fan == NULL) {
        return -1;
    }
    s_fan[id] = fan;
    return 0;
}

int shell_fan_unbind(uint8_t id)
{
    if (id >= SHELL_FAN_MAX || s_fan[id] == NULL) {
        return -1;
    }
    s_fan[id] = NULL;
    return 0;
}

/* 档位：0..1000 原始值，或带 % 的百分比（60% -> 600） */
static bool fan_parse_speed(const char *s, uint16_t *speed)
{
    char *end = NULL;
    double v;

    if (s == NULL || speed == NULL) {
        return false;
    }
    v = strtod(s, &end);
    if (end == s || v < 0.0) {
        return false;
    }
    if (*end == '%') {
        end++;
        v *= 10.0;              /* 100% -> 1000 */
    }
    if (*end != '\0') {
        return false;
    }
    if (v > 1000.0) {
        v = 1000.0;
    }
    *speed = (uint16_t)(v + 0.5);
    return true;
}

#define FAN_USAGE \
    "fan list | fan <id> speed <0-1000|n%> | on | off | rpm | ppr <n> | " \
    "tach | info"

static int fan_usage(shell_t *sh)
{
    (void)sh;
    ros_printf("usage: %s\n", FAN_USAGE);
    return -SHELL_ERR_PARAM;
}

static bool fan_parse_id(const char *s, uint8_t *id)
{
    char *end = NULL;
    unsigned long v;

    if (s == NULL || id == NULL) {
        return false;
    }
    v = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || v >= SHELL_FAN_MAX) {
        return false;
    }
    *id = (uint8_t)v;
    return true;
}

static void fan_print_info(shell_t *sh, uint8_t id)
{
    const fan_t *f = s_fan[id];

    (void)sh;
    ros_printf("fan[%u] duty=%u.%u%% rpm=%u pulses=%u ppr=%u\n",
                 (unsigned)id, (unsigned)(f->duty / 10U),
                 (unsigned)(f->duty % 10U), (unsigned)fan_get_rpm(f),
                 (unsigned)fan_get_pulse_count(f),
                 (unsigned)f->pulses_per_rev);
}

static int cmd_fan(shell_t *sh, int argc, char **argv)
{
    fan_t *f;
    uint8_t id;
    int rc = 0;

    if (argc < 2) {
        return fan_usage(sh);
    }

    if (strcmp(argv[1], "list") == 0) {
        uint8_t i;
        bool any = false;

        for (i = 0U; i < SHELL_FAN_MAX; i++) {
            if (s_fan[i] != NULL) {
                any = true;
                (void)fan_update_rpm(s_fan[i], hal_tick_get());
                fan_print_info(sh, i);
            }
        }
        if (!any) {
            ros_puts("(no fan bound)");
        }
        return SHELL_OK;
    }

    if (!fan_parse_id(argv[1], &id)) {
        shell_error(sh, SHELL_ERR_PARAM, "fan id range: 0..%u",
                    (unsigned)(SHELL_FAN_MAX - 1U));
        return -SHELL_ERR_PARAM;
    }
    if (argc < 3) {
        return fan_usage(sh);
    }
    f = s_fan[id];
    if (f == NULL) {
        shell_error(sh, SHELL_ERR_NOT_FOUND, "fan %u not bound",
                    (unsigned)id);
        return -SHELL_ERR_NOT_FOUND;
    }

    if (strcmp(argv[2], "speed") == 0) {
        uint16_t speed;

        if (argc < 4 || !fan_parse_speed(argv[3], &speed)) {
            return fan_usage(sh);
        }
        rc = fan_set_speed(f, speed);
    } else if (strcmp(argv[2], "on") == 0) {
        rc = fan_on(f);
    } else if (strcmp(argv[2], "off") == 0) {
        rc = fan_off(f);
    } else if (strcmp(argv[2], "ppr") == 0) {
        if (argc < 4) {
            return fan_usage(sh);
        }
        rc = fan_set_pulses_per_rev(f,
                                    (uint16_t)strtoul(argv[3], NULL, 10));
    } else if (strcmp(argv[2], "tach") == 0) {
        fan_tach_pulse(f);      /* 注入一个反馈脉冲，便于联调/自测 */
    } else if (strcmp(argv[2], "rpm") != 0 &&
               strcmp(argv[2], "info") != 0) {
        return fan_usage(sh);
    }

    if (rc != 0) {
        shell_error(sh, SHELL_ERR_DEVICE, "fan %u %s failed: %d",
                    (unsigned)id, argv[2], rc);
        return -SHELL_ERR_DEVICE;
    }
    /* 借用命令执行时刻顺带刷新一次窗口，便于交互观察转速 */
    (void)fan_update_rpm(f, hal_tick_get());
    fan_print_info(sh, id);
    return SHELL_OK;
}

/* ============================== stepper ============================== */

typedef struct {
    bool           ready;
    bool           has_dir;
    hal_pin_t      step_pin;
    hal_pin_t      dir_pin;
    uint32_t       delay_us;    /* 脉冲半周期（高 / 低各一次） */
    stepper_t      st;
    stepper_ops_t  ops;
} shell_step_ctx_t;

static shell_step_ctx_t s_step[SHELL_STEPPER_MAX];

static int step_set_dir(void *ctx, uint8_t dir)
{
    shell_step_ctx_t *c = (shell_step_ctx_t *)ctx;

    if (c == NULL || !c->has_dir) {
        return 0;               /* 无方向脚：只记录软件方向 */
    }
    return (hal_gpio_write(c->dir_pin,
                           (dir != 0U) ? HAL_GPIO_HIGH
                                       : HAL_GPIO_LOW) == HAL_OK) ? 0 : -1;
}

static int step_pulse(void *ctx)
{
    shell_step_ctx_t *c = (shell_step_ctx_t *)ctx;

    if (c == NULL) {
        return -1;
    }
    if (hal_gpio_write(c->step_pin, HAL_GPIO_HIGH) != HAL_OK) {
        return -1;
    }
    hal_delay_us(c->delay_us);
    if (hal_gpio_write(c->step_pin, HAL_GPIO_LOW) != HAL_OK) {
        return -1;
    }
    hal_delay_us(c->delay_us);
    return 0;
}

#define STEPPER_USAGE \
    "stepper list | stepper <id> init <step_pin> [dir_pin] | speed <us> | " \
    "dir <0|1> | move <n> | moveto <n> | pos | info"

static int stepper_usage(shell_t *sh)
{
    (void)sh;
    ros_printf("usage: %s\n", STEPPER_USAGE);
    return -SHELL_ERR_PARAM;
}

/* 实例号：0 .. SHELL_STEPPER_MAX-1 */
static bool stepper_parse_id(const char *s, uint32_t *id)
{
    char *end = NULL;
    unsigned long v;

    if (s == NULL || id == NULL) {
        return false;
    }
    v = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || v >= SHELL_STEPPER_MAX) {
        return false;
    }
    *id = (uint32_t)v;
    return true;
}

static int stepper_require_ready(shell_t *sh, uint32_t id)
{
    if (!s_step[id].ready) {
        shell_error(sh, SHELL_ERR_STATE,
                    "stepper %u not initialized: stepper %u init <step_pin> [dir_pin]",
                    (unsigned)id, (unsigned)id);
        return -SHELL_ERR_STATE;
    }
    return SHELL_OK;
}

static void stepper_print_info(shell_t *sh, uint32_t id)
{
    shell_step_ctx_t *c = &s_step[id];
    char step_name[12];
    char dir_name[12];
    int32_t pos = 0;

    (void)sh;
    (void)stepper_get_position(&c->st, &pos);
    dev_format_pin(c->step_pin, step_name, sizeof(step_name));
    if (c->has_dir) {
        dev_format_pin(c->dir_pin, dir_name, sizeof(dir_name));
    } else {
        dir_name[0] = '-';
        dir_name[1] = '\0';
    }
    ros_printf("stepper[%u] step=%s dir=%s speed=%uus pos=%ld dir_flag=%u\n",
                 (unsigned)id, step_name, dir_name, (unsigned)c->delay_us,
                 (long)pos, (unsigned)c->st.direction);
}

static int cmd_stepper(shell_t *sh, int argc, char **argv)
{
    shell_step_ctx_t *c;
    uint32_t id;
    int rc;

    if (argc < 2) {
        return stepper_usage(sh);
    }

    if (strcmp(argv[1], "list") == 0) {
        uint32_t i;
        bool any = false;

        for (i = 0U; i < SHELL_STEPPER_MAX; i++) {
            if (s_step[i].ready) {
                any = true;
                stepper_print_info(sh, i);
            }
        }
        if (!any) {
            ros_puts("(no stepper initialized)");
        }
        return SHELL_OK;
    }

    if (!stepper_parse_id(argv[1], &id)) {
        shell_error(sh, SHELL_ERR_PARAM, "stepper id range: 0..%u",
                    (unsigned)(SHELL_STEPPER_MAX - 1U));
        return -SHELL_ERR_PARAM;
    }
    c = &s_step[id];

    if (argc < 3) {
        return stepper_usage(sh);
    }

    if (strcmp(argv[2], "init") == 0) {
        hal_pin_t step_pin;
        hal_pin_t dir_pin = 0U;
        bool has_dir = false;

        if (argc < 4 || !dev_parse_pin(argv[3], &step_pin)) {
            return stepper_usage(sh);
        }
        if (argc >= 5) {
            if (!dev_parse_pin(argv[4], &dir_pin)) {
                return stepper_usage(sh);
            }
            has_dir = true;
        }

        rc = hal_gpio_config_output(step_pin, HAL_GPIO_LOW);
        if (rc == HAL_OK && has_dir) {
            rc = hal_gpio_config_output(dir_pin, HAL_GPIO_LOW);
        }
        if (rc != HAL_OK) {
            shell_error(sh, SHELL_ERR_DEVICE,
                        "stepper %u gpio setup failed: %d", (unsigned)id, rc);
            return -SHELL_ERR_DEVICE;
        }

        c->step_pin = step_pin;
        c->dir_pin = dir_pin;
        c->has_dir = has_dir;
        if (c->delay_us == 0U) {
            c->delay_us = SHELL_STEPPER_DELAY_US;
        }
        c->ops.set_dir = has_dir ? step_set_dir : NULL;
        c->ops.step = step_pulse;
        c->ops.ctx = c;
        if (stepper_init(&c->st, &c->ops) != 0) {
            shell_error(sh, SHELL_ERR_MOTOR, "stepper %u init failed",
                        (unsigned)id);
            return -SHELL_ERR_MOTOR;
        }
        c->ready = true;
        stepper_print_info(sh, id);
        return SHELL_OK;
    }

    if (strcmp(argv[2], "speed") == 0) {
        uint32_t us;

        if (argc < 4) {
            return stepper_usage(sh);
        }
        us = (uint32_t)strtoul(argv[3], NULL, 10);
        if (us == 0U || us > SHELL_STEPPER_DELAY_MAX_US) {
            shell_error(sh, SHELL_ERR_PARAM, "speed range: 1..%u us",
                        (unsigned)SHELL_STEPPER_DELAY_MAX_US);
            return -SHELL_ERR_PARAM;
        }
        c->delay_us = us;
        if (c->ready) {
            stepper_print_info(sh, id);
        } else {
            ros_printf("stepper[%u] speed=%uus (not init)\n",
                         (unsigned)id, (unsigned)us);
        }
        return SHELL_OK;
    }

    if (strcmp(argv[2], "dir") == 0) {
        uint8_t dir;

        if (stepper_require_ready(sh, id) != SHELL_OK) {
            return -SHELL_ERR_STATE;
        }
        if (argc < 4 ||
            (strcmp(argv[3], "0") != 0 && strcmp(argv[3], "1") != 0)) {
            return stepper_usage(sh);
        }
        dir = (uint8_t)(argv[3][0] - '0');
        if (stepper_set_direction(&c->st, dir) != 0) {
            shell_error(sh, SHELL_ERR_MOTOR,
                        "stepper %u set direction failed", (unsigned)id);
            return -SHELL_ERR_MOTOR;
        }
        stepper_print_info(sh, id);
        return SHELL_OK;
    }

    if (strcmp(argv[2], "move") == 0 || strcmp(argv[2], "moveto") == 0) {
        int32_t target;

        if (stepper_require_ready(sh, id) != SHELL_OK) {
            return -SHELL_ERR_STATE;
        }
        if (argc < 4 || !dev_parse_i32(argv[3], &target)) {
            return stepper_usage(sh);
        }
        rc = (argv[2][4] == '\0') ? stepper_move(&c->st, target)
                                  : stepper_move_to(&c->st, target);
        if (rc != 0) {
            shell_error(sh, SHELL_ERR_MOTOR, "stepper %u move failed",
                        (unsigned)id);
            return -SHELL_ERR_MOTOR;
        }
        stepper_print_info(sh, id);
        return SHELL_OK;
    }

    if (strcmp(argv[2], "pos") == 0 || strcmp(argv[2], "info") == 0) {
        if (stepper_require_ready(sh, id) != SHELL_OK) {
            return -SHELL_ERR_STATE;
        }
        stepper_print_info(sh, id);
        return SHELL_OK;
    }

    return stepper_usage(sh);
}

/* ============================== 注册 ============================== */

static const shell_cmd_t s_dev_cmds[] = {
    { "gpio", cmd_gpio, GPIO_USAGE,
      "GPIO mode/set/get/toggle, pin like PA5", "device", CMD_REENTRANT },
    { "pwm", cmd_pwm, PWM_USAGE,
      "control bound PWM channels (see shell_pwm_bind)", "device",
      CMD_REENTRANT },
    { "stepper", cmd_stepper, STEPPER_USAGE,
      "step/dir motor over GPIO, speed <us> per half pulse", "device",
      CMD_REENTRANT },
    { "fan", cmd_fan, FAN_USAGE,
      "PWM fan speed/rpm over bound fan instances", "device",
      CMD_REENTRANT }
};

int shell_dev_register(void)
{
    uint32_t i;

    for (i = 0U; i < sizeof(s_dev_cmds) / sizeof(s_dev_cmds[0]); i++) {
        /* 已存在（重复打开 Shell / 同名命令）时跳过，保证幂等 */
        if (shell_cmd_find(s_dev_cmds[i].name) != NULL) {
            continue;
        }
        if (shell_register_cmd(&s_dev_cmds[i]) != 0) {
            return -1;
        }
    }
    return 0;
}
