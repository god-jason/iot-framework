#ifndef BA111_REGISTER_H
#define BA111_REGISTER_H

/*
 * BA111 温度传感器寄存器定义。
 *
 * 注意：BA111 的公开资料较少，下列寄存器布局与换算系数为常见 I2C
 * 温度传感器的通用约定（0x00 温度，16 位有符号，1 LSB = 0.0625 ℃），
 * 未经数据手册确认。若实测不符，可先用 ba111_read_raw 读取原始码，
 * 再用 ba111_read_reg / ba111_write_reg 适配具体命令。
 */

#define BA111_DEFAULT_ADDRESS       0x48U
#define BA111_ADDRESS_MASK          0x7FU

/* 假设的寄存器映射 */
#define BA111_REG_TEMPERATURE       0x00U   /* 16 位有符号温度 */
#define BA111_REG_CONFIG            0x01U
#define BA111_REG_ID                0x0FU   /* 器件 ID（若存在） */

/* 假设的温度换算系数 */
#define BA111_LSB_CELSIUS           0.0625f

/* 上电稳定 / 首次转换时间 */
#define BA111_POWER_ON_DELAY_MS     50U

#endif /* BA111_REGISTER_H */
