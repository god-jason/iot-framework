#ifndef CS100_REGISTER_H
#define CS100_REGISTER_H

/*
 * CS100 NDIR CO2 传感器——UART 模式帧定义。
 *
 * ！！注意！！ CS100 的 UART 协议在不同批次/固件（Cubic 私有协议）中字段
 * 偏移有差异，下列布局按 9 字节定长帧整理，**使用前必须按实际 datasheet
 * 核对**：帧头、命令/长度字段位置、CO2 字段偏移、16 位累加和校验。
 * 字段集中在宏里，实测不符时只改本文件即可。
 */

#define CS100_FRAME_LEN        9U
#define CS100_HEAD             0xEBU
#define CS100_ADDR             0x90U

/* CO2 浓度：大端 16 位，单位 ppm */
#define CS100_OFF_CO2_H        3U
#define CS100_OFF_CO2_L        4U

/* 校验：16 位大端，等于前 7 字节和的补码 */
#define CS100_CHECKSUM_OFF     7U
#define CS100_CHECKSUM_BYTES   7U

/* 量程与分辨率 */
#define CS100_CO2_MIN_PPM      0U
#define CS100_CO2_MAX_PPM      5000U

#endif /* CS100_REGISTER_H */
