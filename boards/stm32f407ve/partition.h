#ifndef BOARD_PARTITION_H
#define BOARD_PARTITION_H

/*
 * partition.h - STM32F407VE（512KB Flash）A/B/C OTA 分区定义
 *
 * F407VE 扇区：4×16KB + 1×64KB + 3×128KB，分区必须扇区对齐。
 *
 * 顺序  区域              起始地址        大小      对应扇区
 * 1     Bootloader        0x08000000      16KB      S0
 * 2     Boot Meta 日志    0x08004000      16KB      S1（追加式，写满擦除）
 * 3     配置主区          0x08008000      16KB      S2
 * 4     配置备份          0x0800C000      16KB      S3
 * 5     系统日志          0x08010000      64KB      S4（文本追加，带 Unix 时间）
 * 6     App 主程序        0x08020000      128KB     S5
 * 7     App 备份          0x08040000      128KB     S6
 * 8     App 升级包        0x08060000      128KB     S7
 *
 * 升级流程：
 *   上位机把新固件（按 App 区地址 0x08020000 链接）经串口 OTA 写入升级包区；
 *   Bootloader 启动时若发现升级包区有效：App -> 备份区 -> 升级包 -> App，
 *   擦除升级包后进入试运行；App 初始化完成调用 boot_confirm() 确认成功，
 *   否则崩溃/重试超限后用备份区恢复 App 区，运行旧程序。
 *
 * Meta 是 S1 内的 64B 追加式记录（共 256 条），写入不擦除，掉电安全；
 * 系统日志是 S4 内的文本追加区，每条以 [unix] 开头，写满后整区擦除重写。
 *
 * 修改后需同步：
 *   - stm32f407ve.ld（App=0x08020000）/ boot/bootloader.ld
 *   - xmake.lua 的 APP_VECTOR_BASE=0x08020000U
 *   - tools/crash_log.py 的 F4 日志转储起始地址 0x08010000（64KB）
 */

#define BOOT_BASE               0x08000000U
#define BOOT_SIZE               (16U * 1024U)

#define BOOT_META_BASE          0x08004000U
#define BOOT_META_SIZE          (16U * 1024U)
#define BOOT_META_SLOT          (64U)
#define BOOT_META_RECORDS       (BOOT_META_SIZE / BOOT_META_SLOT)

#define BOOT_CONFIG_BASE0       0x08008000U
#define BOOT_CONFIG_BASE1       0x0800C000U
#define BOOT_CONFIG_SIZE        (16U * 1024U)

#define BOOT_LOG_BASE           0x08010000U
#define BOOT_LOG_SIZE           (64U * 1024U)

#define BOOT_SLOT_A_BASE        0x08020000U
#define BOOT_SLOT_B_BASE        0x08040000U
#define BOOT_SLOT_C_BASE        0x08060000U
#define BOOT_SLOT_SIZE          (128U * 1024U)

#endif /* BOARD_PARTITION_H */
