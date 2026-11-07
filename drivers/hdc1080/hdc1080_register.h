#ifndef HDC1080_REGISTER_H
#define HDC1080_REGISTER_H

/*
 * HDC1080 寄存器定义（I2C，7 位地址 0x40）。
 *
 * 读取流程（寄存器指针式）：
 *   写指针 0x00 -> 等待转换 -> 读 2 字节得到温度（先触发温度，再触发湿度）
 *   写指针 0x01 -> 等待转换 -> 读 2 字节得到湿度
 * 两者均为 16 位无符号、右对齐，转换时间 14 位约 6.35ms。
 */

#define HDC1080_DEFAULT_ADDRESS        0x40U
#define HDC1080_ADDRESS_ALT            0x41U

/* 寄存器地址 */
#define HDC1080_REG_TEMPERATURE        0x00U
#define HDC1080_REG_HUMIDITY           0x01U
#define HDC1080_REG_CONFIGURATION      0x02U
#define HDC1080_REG_SERIAL_HIGH        0xFBU   /* 器件 ID 也由 0xFB/0xFC 返回 */
#define HDC1080_REG_DEVICE_ID          0xFDU

/* 器件 ID（0xFB 起始 2 字节） */
#define HDC1080_DEVICE_ID              0x1050U

/* 配置寄存器位 */
#define HDC1080_CONFIG_RST             0x8000U  /* 软复位（自清零） */
#define HDC1080_CONFIG_HEAT            0x2000U  /* 加热器 */
#define HDC1080_CONFIG_MODE            0x1000U  /* 1: 温湿度顺序采集 */
#define HDC1080_CONFIG_TRES_11BIT      0x0400U
#define HDC1080_CONFIG_HRES_11BIT      0x0200U
#define HDC1080_CONFIG_HRES_8BIT       0x0100U
/* 0x0000: 温度 14 位、湿度 14 位、加热器关、独立采集模式 */
#define HDC1080_CONFIG_14BIT_BOTH      0x0000U

/* 时序（ms） */
#define HDC1080_RESET_DELAY_MS         15U
#define HDC1080_CONVERT_DELAY_MS       10U   /* 14 位最大约 6.35ms */

/* 换算：T = raw / 2^16 * 165 - 40，RH = raw / 2^16 * 100 */
#define HDC1080_RAW_MAX                65536.0f
#define HDC1080_TEMP_SCALE             165.0f
#define HDC1080_TEMP_OFFSET            (-40.0f)
#define HDC1080_HUMIDITY_SCALE         100.0f

#endif /* HDC1080_REGISTER_H */
