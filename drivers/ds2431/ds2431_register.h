#ifndef DS2431_REGISTER_H
#define DS2431_REGISTER_H

/*
 * DS2431 1-Wire EEPROM 命令与时序参数。
 * 参考 Maxim DS2431 datasheet（1024 bit，4 页 x 32 字节）。
 */

/* ---------------- ROM 命令 ---------------- */
#define DS2431_CMD_READ_ROM        0x33U
#define DS2431_CMD_MATCH_ROM       0x55U
#define DS2431_CMD_SEARCH_ROM      0xF0U
#define DS2431_CMD_SKIP_ROM        0xCCU
#define DS2431_CMD_RESUME          0xA5U

/* ---------------- 存储命令 ---------------- */
#define DS2431_CMD_READ_MEMORY     0xF0U
#define DS2431_CMD_WRITE_SCRATCH   0x0FU
#define DS2431_CMD_READ_SCRATCH    0xAAU
#define DS2431_CMD_COPY_SCRATCH    0x55U

/* ---------------- 容量 ---------------- */
#define DS2431_MEM_SIZE            128U
#define DS2431_PAGE_SIZE           32U
#define DS2431_SCRATCH_DATA        8U
#define DS2431_SCRATCHPAD_SIZE     13U   /* TA1+TA2+ES+8B 数据+2B CRC16 */
#define DS2431_ROM_SIZE            8U

/* ---------------- 时序（微秒） ---------------- */
#define DS2431_RESET_LOW_US        480U
#define DS2431_RESET_RELEASE_US    70U
#define DS2431_PRESENCE_WAIT_US    410U
#define DS2431_WRITE1_LOW_US       6U
#define DS2431_WRITE_RECOVERY_US   10U
#define DS2431_WRITE0_LOW_US       60U
#define DS2431_READ_INIT_US        3U
#define DS2431_READ_SAMPLE_US      10U
#define DS2431_READ_TAIL_US        55U

/* Copy Scratchpad 后 EEPROM 写入时间 tPROG */
#define DS2431_COPY_DELAY_MS       12U

/* page 长度内结束地址掩码 */
#define DS2431_PAGE_MASK           0x1FU

#endif /* DS2431_REGISTER_H */
