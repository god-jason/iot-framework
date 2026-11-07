#ifndef VL53L1X_REGISTER_H
#define VL53L1X_REGISTER_H

/*
 * VL53L1X ToF 激光测距传感器寄存器定义（16 位寄存器地址，I2C 0x29）。
 *
 * 寄存器编号取自 ST VL53L1X API / 数据手册（大端 16 位地址）。
 * 仅覆盖基础测距所需的寄存器；ROI / timing budget / 相位校准等
 * 配置寄存器未列入，需按 ST API 补充后自行添加。
 */

#define VL53L1X_DEFAULT_ADDRESS                     0x29U

/* ---------------- 寄存器（16 位地址） ---------------- */
#define VL53L1X_REG_SOFT_RESET                      0x0000U
#define VL53L1X_REG_GPIO__TIO_HV_STATUS             0x0031U
#define VL53L1X_REG_SYSTEM_INTERRUPT_CONFIG_GPIO    0x0046U
#define VL53L1X_REG_SYSTEM_INTERRUPT_CLEAR          0x0086U
#define VL53L1X_REG_SYSTEM_START                    0x0087U
#define VL53L1X_REG_RESULT__RANGE_STATUS            0x0089U
#define VL53L1X_REG_RESULT__RANGE_MM                0x0096U  /* 串扰校正后距离 */
#define VL53L1X_REG_FIRMWARE__SYSTEM_STATUS         0x00E5U
#define VL53L1X_REG_IDENTIFICATION__MODEL_ID        0x010FU

/* ---------------- 取值 ---------------- */
#define VL53L1X_MODEL_ID_VALUE                      0xEAU
#define VL53L1X_RESET_OFF                           0x00U
#define VL53L1X_RESET_ON                            0x01U
#define VL53L1X_SYSTEM_START_DSS                    0x40U  /* 启动测距 */
#define VL53L1X_SYSTEM_STOP                         0x00U
#define VL53L1X_INTERRUPT_CLEAR_ALL                 0x01U
#define VL53L1X_FW_BOOT_MASK                        0x01U  /* 1 = 固件启动完成 */
#define VL53L1X_INT_STATUS_MASK                     0x01U  /* 0 = 新样本就绪 */
#define VL53L1X_RANGE_STATUS_MASK                   0x1FU

/* ---------------- 时序 ---------------- */
#define VL53L1X_RESET_DELAY_MS                      2U
#define VL53L1X_POLL_INTERVAL_MS                    2U
#define VL53L1X_RANGE_TIMEOUT_MS                    500U

#endif /* VL53L1X_REGISTER_H */
