#ifndef FM24CLXX_REGISTER_H
#define FM24CLXX_REGISTER_H

/*
 * FM24CLxx FRAM 常量：16 位地址，无写延时、无页边界限制，
 * 分块上限仅用于适配 I2C 单次传输长度。
 */

#define FM24CLXX_DEFAULT_ADDRESS   0x50U
#define FM24CLXX_CHUNK             32U

#define FM24CLXX_CAPACITY_MIN      512U      /* FM24CL04 */
#define FM24CLXX_CAPACITY_MAX      65536U    /* FM24CL512 */

#endif /* FM24CLXX_REGISTER_H */
