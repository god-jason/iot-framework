#ifndef EM4095_REGISTER_H
#define EM4095_REGISTER_H

/*
 * EM4095 读头前端时序参数（无寄存器，只有引脚与时钟）。
 *
 * RDY_CLK 典型频率 2.5kHz 左右，DOUT 为 Manchester 编码；上升沿后
 * 的采样延时与位周期需按实际天线/谐振回路标定。
 */

#define EM4095_SAMPLE_DELAY_US   100U   /* RDY_CLK 上升沿 -> 采样 DOUT */
#define EM4095_EDGE_POLL_US      10U    /* 边沿等待轮询间隔 */
#define EM4095_RDY_CLK_MIN_HZ    500U
#define EM4095_RDY_CLK_MAX_HZ    5000U

#endif /* EM4095_REGISTER_H */
