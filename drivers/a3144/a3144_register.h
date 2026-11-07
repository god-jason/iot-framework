#ifndef A3144_REGISTER_H
#define A3144_REGISTER_H

/*
 * A3144 霍尔开关电气/时序参数。
 *
 * 器件为开漏（OC）输出，OUT 必须外接上拉电阻（典型 10k）。
 * 磁场（南极）靠近时内部三极管导通，OUT 被拉低，因此默认低电平有效。
 */

/* 有效电平：0 表示检测到磁场时 OUT 为低电平（datasheet 默认接法） */
#define A3144_ACTIVE_LEVEL      0U

/* 上电/电平变化后的稳定时间（us） */
#define A3144_SETTLE_US         100U

#endif /* A3144_REGISTER_H */
