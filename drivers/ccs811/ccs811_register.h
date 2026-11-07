#ifndef CCS811_REGISTER_H
#define CCS811_REGISTER_H

/*
 * CCS811 eCO2 / TVOC 气体传感器寄存器定义（8 位寄存器地址，I2C 0x5A）。
 *
 * 流程要点（ams 数据手册）：
 *   1) 读 HW_ID(0x20) 必须为 0x81；
 *   2) STATUS(0x00) 的 APP_VALID 必须为 1，否则器件仍在 bootloader；
 *   3) 向 APP_START(0xF4) 写入即从 boot 模式切换到 application 模式，
 *      STATUS.FW_MODE 置 1 表示切换完成（切换瞬间会 NACK，需重试）；
 *   4) MEAS_MODE(0x01) 选择测量周期，然后轮询 STATUS.DATA_READY。
 */

#define CCS811_DEFAULT_ADDRESS        0x5AU

/* ---------------- 寄存器 ---------------- */
#define CCS811_REG_STATUS             0x00U
#define CCS811_REG_MEAS_MODE          0x01U
#define CCS811_REG_ALG_RESULT_DATA    0x02U
#define CCS811_REG_ENV_DATA           0x05U
#define CCS811_REG_NTC                0x06U
#define CCS811_REG_THRESHOLDS         0x10U
#define CCS811_REG_BASELINE           0x11U
#define CCS811_REG_HW_ID              0x20U
#define CCS811_REG_HW_VERSION         0x21U
#define CCS811_REG_FW_BOOT_VERSION    0x23U
#define CCS811_REG_FW_APP_VERSION     0x24U
#define CCS811_REG_ERROR_ID           0xE0U
#define CCS811_REG_APP_START          0xF4U
#define CCS811_REG_SW_RESET           0xFFU

/* ---------------- 取值 ---------------- */
#define CCS811_HW_ID_VALUE            0x81U

/* STATUS 位 */
#define CCS811_STATUS_ERROR           0x01U  /* 1 = 存在错误，读 ERROR_ID */
#define CCS811_STATUS_DATA_READY      0x08U  /* 1 = 有新的算法结果 */
#define CCS811_STATUS_APP_VALID       0x10U  /* 1 = application 固件有效 */
#define CCS811_STATUS_FW_MODE         0x80U  /* 1 = 运行于 application 模式 */

/* MEAS_MODE：mode1 = 每秒一次测量，关闭中断与阈值 */
#define CCS811_MEAS_MODE_1S           0x10U
#define CCS811_MEAS_MODE_MASK         0x70U

/* APP_START：数据手册形式是无数据写；部分端口层不接受 0 长度，
 * 退化为写 1 字节 0x01（旧实现兼容值）。 */
#define CCS811_APP_START_LEGACY_DATA  0x01U

/* SW_RESET 魔数 */
#define CCS811_SW_RESET_MAGIC_0       0x11U
#define CCS811_SW_RESET_MAGIC_1       0xE5U
#define CCS811_SW_RESET_MAGIC_2       0x72U
#define CCS811_SW_RESET_MAGIC_3       0x8AU

/* ALG_RESULT_DATA 块长度：eCO2(2) + TVOC(2) + status + error_id + raw(2) */
#define CCS811_RESULT_LEN             8U

/* ENV_DATA 系数：湿度 1/512 %RH，温度 1/512 ℃（相对 25℃） */
#define CCS811_ENV_SCALE              512.0f
#define CCS811_ENV_TEMP_OFFSET        25.0f

/* ---------------- 时序 ---------------- */
#define CCS811_RESET_DELAY_MS         20U
#define CCS811_APP_START_DELAY_MS     20U
#define CCS811_POLL_INTERVAL_MS       20U
#define CCS811_READY_TIMEOUT_MS       1500U

#endif /* CCS811_REGISTER_H */
