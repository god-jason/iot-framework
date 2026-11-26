/*
 * gpio.c - GPIO Shell 命令实现
 *
 * 命令注册在 cmd 全局命令表，所有 Shell 实例共享。
 * 只依赖 HAL 的 hal_gpio_* 接口，不包含板级细节。
 */

#include "gpio.h"

#include "hal.h"
#include "roshell.h"
#include "ros.h"
#include "common.h"

#include <stdio.h>

/* ============================== 解析辅助 ============================== */

/* 解析引脚：PA5 / pb12 / 0x0105（十进制或十六进制原始编码） */
static bool gpio_parse_pin(const char *s, uint16_t *pin)
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

/* 格式化为 PA5 形式，便于打印 */
static void gpio_format_pin(uint16_t pin, char *buf, size_t cap)
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

static int gpio_apply(int rc, const char *what, uint16_t pin)
{
    char name[12];

    gpio_format_pin(pin, name, sizeof(name));
    if (rc != HAL_OK) {
        ros_printf("ERR %d: gpio %s %s failed: %d\n", -1, what, name, rc);
        return -1;
    }
    ros_printf("gpio %s %s ok\n", what, name);
    return 0;
}

/* ============================== gpio 命令 ============================== */

static int gpio_cmd(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)rs;
    (void)argc;
    (void)argv;
    (void)user;
    const char *action = NULL;
    const char *pin_s = NULL;
    const char *a1 = NULL;
    const char *a2 = NULL;
    uint16_t pin;
    int rc;

    if (!rs_args_get_str(parsed, 0U, &action) ||
        !rs_args_get_str(parsed, 1U, &pin_s) ||
        action == NULL || pin_s == NULL ||
        !gpio_parse_pin(pin_s, &pin)) {
        ros_puts("usage: gpio mode <pin> in|out [pull] | set <pin> <0|1> | "
                 "get <pin> | toggle <pin> | deinit <pin>");
        return -1;
    }
    (void)rs_args_get_str(parsed, 2U, &a1);
    (void)rs_args_get_str(parsed, 3U, &a2);

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
        gpio_format_pin(pin, name, sizeof(name));
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
    s_gpio_args, 4U, gpio_cmd, NULL, NULL, NULL, NULL
};

int gpio_cmd_register(void)
{
    static bool ready;

    if (ready) {
        return 0;
    }
    if (rs_cmd_register(&s_gpio_def) != 0) {
        return -1;
    }
    ready = true;
    return 0;
}
