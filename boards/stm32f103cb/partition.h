#ifndef BOARD_PARTITION_H
#define BOARD_PARTITION_H

/*
 * partition.h - STM32F103CBT6（128KB Flash）单 App 槽 + OTA 进度日志分区
 *
 * 顺序  区域              起始地址        大小
 * 1     Bootloader        0x08000000      8KB
 * 2     配置主区          0x08002000      4KB
 * 3     配置备份          0x08004000      4KB
 * 4     系统日志          0x08006000      4KB（文本追加，带 Unix 时间）
 * 5     升级日志 OTA Log  0x08007000      4KB（升级有效标识 + 进度记录）
 * 6     主程序 App        0x08008000      52KB
 * 7     升级包 OTA        0x08015000      52KB
 *
 * 升级流程（无 A/B 双槽）：
 *   1. 串口 OTA 把新固件（按 App 区 0x08008000 链接）写入升级包区；
 *   2. 接收完成并校验 CRC 后，在升级日志区追加一条 READY 记录，
 *      该记录同时就是“升级有效标识”；
 *   3. Bootloader 下次启动擦除 App 区，按 1KB 分块把升级包拷入 App 区，
 *      每拷完一块追加一条进度记录（含已写字节数）；掉电后从最后一条
 *      记录继续，保证 App 区最终是完整镜像；
 *   4. 全部写完并整段 CRC 校验通过后，擦除升级日志分区（清除有效标识），
 *      然后启动 App。
 *
 * 修改后需同步：
 *   - stm32f103cb.ld（App=0x08008000）/ boot/bootloader.ld
 *   - xmake.lua 的 APP_VECTOR_BASE=0x08008000U
 *   - tools/crash_log.py 的 F1 日志转储起始地址 0x08006000（4KB）
 */

#define BOOT_BASE               0x08000000U
#define BOOT_SIZE               (8U * 1024U)

#define BOOT_CONFIG_BASE0       0x08002000U
#define BOOT_CONFIG_BASE1       0x08004000U
#define BOOT_CONFIG_SIZE        (4U * 1024U)

#define BOOT_LOG_BASE           0x08006000U
#define BOOT_LOG_SIZE           (4U * 1024U)

#define BOOT_OTA_LOG_BASE       0x08007000U
#define BOOT_OTA_LOG_SIZE       (4U * 1024U)

#define BOOT_APP_BASE           0x08008000U
#define BOOT_APP_SIZE           (52U * 1024U)

#define BOOT_OTA_BASE           0x08015000U
#define BOOT_OTA_SIZE           (52U * 1024U)

#endif /* BOARD_PARTITION_H */
