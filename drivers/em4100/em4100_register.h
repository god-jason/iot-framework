#ifndef EM4100_REGISTER_H
#define EM4100_REGISTER_H

/*
 * EM4100/EM4102 125kHz 只读卡帧格式常量。
 *
 * 64 位帧结构：9 个前导 1 + 10 行 x (4 数据位 + 1 偶校验位)
 * + 4 位列偶校验 + 1 停止位，采用 Manchester 编码。
 */

#define EM4100_FRAME_BITS        64U
#define EM4100_HALF_BITS         (EM4100_FRAME_BITS * 2U)   /* 128 个半位 */
#define EM4100_HEADER_BITS       9U
#define EM4100_ROWS              10U
#define EM4100_COLS              4U
#define EM4100_ID_NIBBLES        10U
#define EM4100_ID_BYTES          5U

/* 采样缓冲：整帧 + 少量冗余，便于解码时搜索起始相位 */
#define EM4100_SAMPLES           (EM4100_HALF_BITS + 8U)
#define EM4100_EDGE_POLL_US      10U

/* 典型半位时长（RF/64 = 125kHz/64 ≈ 512us） */
#define EM4100_HALF_BIT_US       512U

#endif /* EM4100_REGISTER_H */
