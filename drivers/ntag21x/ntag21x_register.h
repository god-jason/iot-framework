#ifndef NTAG21X_REGISTER_H
#define NTAG21X_REGISTER_H

/*
 * NTAG21x 存储区常量（I2C 访问：页号作为寄存器地址，每页 4 字节）。
 */

#define NTAG21X_DEFAULT_ADDRESS   0x55U   /* 7 位从机地址 */
#define NTAG21X_PAGE_SIZE         4U
#define NTAG21X_PAGE_MAX          255U    /* 页地址为 1 字节 */

/* 用户区页数 */
#define NTAG21X_213_USER_PAGES    45U
#define NTAG21X_215_USER_PAGES    135U
#define NTAG21X_216_USER_PAGES    231U

/* 写周期（EEPROM 仿真）最长等待，单位 ms */
#define NTAG21X_WRITE_DELAY_MS    10U

#endif /* NTAG21X_REGISTER_H */
