#ifndef MAX7219_REGISTER_H
#define MAX7219_REGISTER_H

/* MAX7219 寄存器地址（16 位帧：高 8 位地址 + 低 8 位数据） */

#define MAX7219_REG_NOOP            0x00U
#define MAX7219_REG_DIGIT0          0x01U
#define MAX7219_REG_DIGIT1          0x02U
#define MAX7219_REG_DIGIT2          0x03U
#define MAX7219_REG_DIGIT3          0x04U
#define MAX7219_REG_DIGIT4          0x05U
#define MAX7219_REG_DIGIT5          0x06U
#define MAX7219_REG_DIGIT6          0x07U
#define MAX7219_REG_DIGIT7          0x08U
#define MAX7219_REG_DECODE_MODE     0x09U
#define MAX7219_REG_INTENSITY       0x0AU
#define MAX7219_REG_SCAN_LIMIT      0x0BU
#define MAX7219_REG_SHUTDOWN        0x0CU
#define MAX7219_REG_DISPLAY_TEST    0x0FU

#define MAX7219_DIGIT_COUNT         8U
#define MAX7219_INTENSITY_MAX       0x0FU

/* 初始化值 */
#define MAX7219_DECODE_NONE         0x00U
#define MAX7219_SCAN_ALL            0x07U
#define MAX7219_NORMAL_OPERATION    0x01U
#define MAX7219_SHUTDOWN_MODE       0x00U
#define MAX7219_TEST_OFF            0x00U
#define MAX7219_INTENSITY_DEFAULT   0x07U

#endif /* MAX7219_REGISTER_H */
