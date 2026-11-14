/* robotos 综合示例：日志 + LED 闪烁 + 真实延时。
 * 命令行 Shell 交互示例见 ros-shell（examples/shell_demo.c）。 */
#include "ros.h"
#include "common.h"
#include "hal.h"
#include "led.h"

#include <stdio.h>

int main(void)
{
    led_t led;
    int i;

    led_init(&led, 0, 1);
    printf("robotos demo ready\n");

    /* LED 闪烁：每秒翻转一次，共 5 个周期 */
    for (i = 0; i < 5; i++) {
        led_toggle(&led);
        LOG_I("demo", "tick %d", i);
        hal_delay_ms(1000);
    }

    printf("\ndemo finished\n");
    return 0;
}
