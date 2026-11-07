#ifndef VL6180X_REGISTER_H
#define VL6180X_REGISTER_H

/*
 * VL6180X ToF 测距 / 接近传感器寄存器定义（16 位寄存器地址，I2C 0x29）。
 *
 * 注意：VL6180X 的寄存器地址是 16 位（大端），不是 8 位；
 * 旧版驱动按单字节地址访问是错误的，本驱动统一使用 16 位访问。
 * 寄存器编号取自 ST VL6180X 数据手册 / 参考驱动（Pololu、ST API 一致）。
 */

#define VL6180X_DEFAULT_ADDRESS                 0x29U

/* ---------------- 寄存器（16 位地址） ---------------- */
#define VL6180X_REG_IDENTIFICATION__MODEL_ID    0x0000U
#define VL6180X_REG_SYSTEM__FRESH_OUT_OF_RESET  0x0016U
#define VL6180X_REG_SYSRANGE__START             0x0018U
#define VL6180X_REG_SYSRANGE__INTERMEASUREMENT_PERIOD 0x001BU
#define VL6180X_REG_SYSTEM__INTERRUPT_CONFIG_GPIO 0x0046U
#define VL6180X_REG_SYSTEM__INTERRUPT_CLEAR     0x004BU
#define VL6180X_REG_RESULT__RANGE_STATUS        0x004DU
#define VL6180X_REG_RESULT__INTERRUPT_STATUS_GPIO 0x004FU
#define VL6180X_REG_RESULT__RANGE_VAL           0x0062U

/* ---------------- 取值 ---------------- */
#define VL6180X_MODEL_ID_VALUE                  0xB4U
#define VL6180X_SYSRANGE_START_SINGLE           0x01U
#define VL6180X_SYSRANGE_STOP                   0x00U
#define VL6180X_FRESH_OUT_OF_RESET_CLEAR        0x00U
/* GPIO1 作为 range 中断输出（ST 参考实现取值） */
#define VL6180X_INTERRUPT_CONFIG_RANGE          0x24U
#define VL6180X_INTERRUPT_CLEAR_ALL             0x07U
#define VL6180X_INT_STATUS_RANGE_MASK           0x04U  /* bit2 = 新距离样本 */
#define VL6180X_RANGE_STATUS_MASK               0x0FU
#define VL6180X_INTERMEASUREMENT_PERIOD_MS      100U

/* ---------------- 时序 ---------------- */
#define VL6180X_POLL_INTERVAL_MS                2U
#define VL6180X_RANGE_TIMEOUT_MS                200U

#endif /* VL6180X_REGISTER_H */
