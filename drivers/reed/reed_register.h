#ifndef REED_REGISTER_H
#define REED_REGISTER_H

/*
 * 干簧管（磁簧开关）电气参数。
 *
 * 常见接法：一端接 GND，另一端接 GPIO 并加上拉电阻；
 * 磁铁靠近时触点闭合，引脚被拉低，因此默认低电平有效。
 */

/* 有效电平：0 表示触点闭合时读到低电平 */
#define REED_ACTIVE_LEVEL       0U

/* 触点抖动/稳定时间（us），机械触点抖动量级为百微秒 */
#define REED_SETTLE_US          1000U

#endif /* REED_REGISTER_H */
