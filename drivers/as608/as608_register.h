#ifndef AS608_REGISTER_H
#define AS608_REGISTER_H

/*
 * AS608 光学指纹模块 UART 协议常量。
 *
 * 包格式：0xEF 0x01 | 地址(4B) | 包标识(1B) | 包长度(2B, 大端) |
 *         指令/确认码(1B) | 参数 | 校验和(2B)
 * 包长度 = 指令(1) + 参数(n) + 校验和(2) = n + 3
 * 校验和 = 从包标识到参数末字节的累加和。
 */

#define AS608_HEAD0                 0xEFU
#define AS608_HEAD1                 0x01U
#define AS608_ADDRESS_DEFAULT       0xFFFFFFFFUL
#define AS608_PASSWORD_DEFAULT      0x00000000UL
#define AS608_PACKAGE_ID            0x01U

#define AS608_HEADER_SIZE           9U
#define AS608_CHECKSUM_SIZE         2U
#define AS608_PARAM_MAX             32U
#define AS608_RESPONSE_DATA_MAX     64U

/* 指令码 */
#define AS608_CMD_GET_IMAGE         0x01U
#define AS608_CMD_IMAGE2TZ          0x02U
#define AS608_CMD_MATCH             0x03U
#define AS608_CMD_SEARCH            0x04U
#define AS608_CMD_REG_MODEL         0x05U
#define AS608_CMD_STORE             0x06U
#define AS608_CMD_LOAD_CHAR         0x07U
#define AS608_CMD_UP_CHAR           0x08U
#define AS608_CMD_DOWN_CHAR         0x09U
#define AS608_CMD_UP_IMAGE          0x0AU
#define AS608_CMD_DOWN_IMAGE        0x0BU
#define AS608_CMD_DELETE_CHAR       0x0CU
#define AS608_CMD_EMPTY             0x0DU
#define AS608_CMD_READ_SYSPARA      0x0FU
#define AS608_CMD_SET_PASSWORD      0x12U
#define AS608_CMD_VERIFY_PASSWORD   0x13U
#define AS608_CMD_GET_RANDOM_CODE   0x14U
#define AS608_CMD_READ_INF          0x16U
#define AS608_CMD_TEMPLATE_COUNT    0x1DU
#define AS608_CMD_LED               0x35U

/* 确认码 */
#define AS608_CONFIRM_OK            0x00U
#define AS608_CONFIRM_NO_FINGER     0x02U
#define AS608_CONFIRM_NOT_FOUND     0x09U
#define AS608_CONFIRM_MISMATCH      0x0AU
#define AS608_CONFIRM_FLASH_ERROR   0x18U

#endif /* AS608_REGISTER_H */
