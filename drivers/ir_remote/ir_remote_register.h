#ifndef IR_REMOTE_REGISTER_H
#define IR_REMOTE_REGISTER_H

/*
 * NEC 红外协议时序（单位 us）。
 * 帧结构：9ms 引导 mark + 4.5ms space + 32 位（LSB 先）
 *         每位 560us mark + 560us(0)/1690us(1) space + 560us 结束 mark。
 */

#define IR_NEC_LEADER_MARK        9000U
#define IR_NEC_LEADER_SPACE       4500U
#define IR_NEC_BIT_MARK           560U
#define IR_NEC_ZERO_SPACE         560U
#define IR_NEC_ONE_SPACE          1690U
#define IR_NEC_STOP_MARK          560U

/* 解码容差（us） */
#define IR_NEC_LEADER_MARK_MIN    7000U
#define IR_NEC_LEADER_MARK_MAX    11000U
#define IR_NEC_LEADER_SPACE_MIN   3500U
#define IR_NEC_LEADER_SPACE_MAX   5500U
#define IR_NEC_MARK_MIN           300U
#define IR_NEC_MARK_MAX           900U
#define IR_NEC_SPACE_ZERO_MIN     350U
#define IR_NEC_SPACE_ZERO_MAX     900U
#define IR_NEC_SPACE_ONE_MIN      1200U
#define IR_NEC_SPACE_ONE_MAX      2000U

/* 采样参数 */
#define IR_POLL_US                20U
#define IR_IDLE_US                8000U    /* 超过该间隔视为帧结束 */
#define IR_MAX_SEGMENT_US         20000U

#define IR_NEC_BITS               32U
#define IR_NEC_TIMINGS_MIN        (2U + IR_NEC_BITS * 2U)
#define IR_NEC_CARRIER_KHZ_DEFAULT 38U

#endif /* IR_REMOTE_REGISTER_H */
