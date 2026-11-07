#ifndef MCP4725_REGISTER_H
#define MCP4725_REGISTER_H

/* MCP4725 12 位 DAC 命令与位定义（I2C，地址 0x60~0x67） */

#define MCP4725_DEFAULT_ADDRESS     0x60U
#define MCP4725_ADDRESS_MAX         0x67U

/* 命令类型（写命令字节高 3 位 C2C1C0） */
#define MCP4725_CMD_FAST_WRITE      0x00U   /* 0000：快速写（2 字节，不写 EEPROM） */
#define MCP4725_CMD_WRITE_DAC       0x40U   /* 0100：写 DAC 寄存器（3 字节） */
#define MCP4725_CMD_WRITE_DAC_EE    0x60U   /* 0110：写 DAC 并写 EEPROM（3 字节） */

/* 命令字节中的掉电模式位置（bit2:1） */
#define MCP4725_PD_SHIFT            1U
#define MCP4725_PD_MASK             0x06U

/* 12 位数据拆分 */
#define MCP4725_DATA_SHIFT          4U
#define MCP4725_DATA_MASK           0x0FF0U
#define MCP4725_DATA_LOW_MASK       0x0FU

#define MCP4725_RESOLUTION          4096U
#define MCP4725_FULL_SCALE          4095U
#define MCP4725_MAX_VREF_MV         5000U

/* EEPROM 写入时间（datasheet：最大 50ms），留足余量 */
#define MCP4725_EEPROM_WRITE_MS     50U

/* 读回帧长度：状态/DAC/EEPROM 共 5 字节 */
#define MCP4725_READ_LEN            5U

#endif /* MCP4725_REGISTER_H */
