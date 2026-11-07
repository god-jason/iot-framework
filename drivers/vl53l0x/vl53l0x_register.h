#ifndef VL53L0X_REGISTER_H
#define VL53L0X_REGISTER_H

/*
 * VL53L0X ToF 激光测距传感器寄存器定义（8 位寄存器地址，I2C 0x29）。
 *
 * 寄存器编号取自 ST VL53L0X 数据手册 / VL53L0X API（vl53l0x_device.h）。
 * 注意：本驱动只实现"复位 + ID 校验 + 单次测距"的基础路径，
 * ST 官方 API 中的 TuningSettings、SPAD 管理、参考校准未包含。
 */

#define VL53L0X_DEFAULT_ADDRESS             0x29U

/* ---------------- 寄存器 ---------------- */
#define VL53L0X_REG_SYSRANGE_START          0x00U
#define VL53L0X_REG_SYSTEM_INTERRUPT_CLEAR  0x0BU
#define VL53L0X_REG_RESULT_INTERRUPT_STATUS 0x13U
#define VL53L0X_REG_RESULT_RANGE_STATUS     0x14U
#define VL53L0X_REG_SOFT_RESET              0xBFU
#define VL53L0X_REG_MODEL_ID                0xC0U
#define VL53L0X_REG_REVISION_ID             0xC2U

/* ---------------- 取值 ---------------- */
#define VL53L0X_MODEL_ID_VALUE              0xEEU
#define VL53L0X_SYSRANGE_START_SINGLE       0x01U
#define VL53L0X_INTERRUPT_NEW_SAMPLE_MASK   0x07U
#define VL53L0X_INTERRUPT_CLEAR_ALL         0x01U

/* 测距结果块：从 RESULT_RANGE_STATUS(0x14) 起 12 字节，
 * 距离(mm) 位于块内偏移 10（大端），范围状态位于块内首字节 bit6:3。 */
#define VL53L0X_RESULT_BLOCK_LEN            12U
#define VL53L0X_RESULT_RANGE_OFFSET         10U
#define VL53L0X_RANGE_STATUS_MASK           0x78U
#define VL53L0X_RANGE_STATUS_SHIFT          3U

/* ---------------- 时序 ---------------- */
#define VL53L0X_RESET_DELAY_MS              2U
#define VL53L0X_POLL_INTERVAL_MS            2U
#define VL53L0X_RANGE_TIMEOUT_MS            200U

#endif /* VL53L0X_REGISTER_H */
