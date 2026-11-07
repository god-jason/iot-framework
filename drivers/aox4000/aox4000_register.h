#ifndef AOX4000_REGISTER_H
#define AOX4000_REGISTER_H

/*
 * AOX4000 氧气传感器——UART 主动上报 / 应答模式。
 *
 * ！！注意！！ 本文件中的帧布局按常见 9 字节定长帧约定整理，未获得原厂
 * datasheet 全文核对，使用前必须与实际模块的 datasheet 逐字段确认：
 *   - 帧头字节、字段偏移、量纲（0.01 vol% 还是 0.1 vol%）
 *   - 校验方式（本实现使用“字节累加和取反加一”占位）
 * 若实测不符，只需修改本文件的宏即可，.c 中的解析逻辑无需改动。
 */

#define AOX4000_FRAME_LEN        9U
#define AOX4000_HEAD0            0xAAU
#define AOX4000_HEAD1            0x55U

/* 字段偏移（待核对） */
#define AOX4000_OFF_O2_H         3U
#define AOX4000_OFF_O2_L         4U
#define AOX4000_OFF_CHECKSUM     8U

/* O2 原始值单位：0.01 vol%（如 2098 -> 20.98 %） */
#define AOX4000_O2_LSB_PERCENT   0.01f

/* 有效量程（vol%） */
#define AOX4000_O2_MIN_PERCENT   0.0f
#define AOX4000_O2_MAX_PERCENT   25.0f

#endif /* AOX4000_REGISTER_H */
