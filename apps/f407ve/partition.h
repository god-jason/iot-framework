#ifndef BOARD_PARTITION_H
#define BOARD_PARTITION_H

/*
 * partition.h - STM32F407VE（512KB Flash）APP/BACKUP/OTA 分区定义
 *
 * F407VE 扇区：4×16KB + 1×64KB + 3×128KB，分区必须扇区对齐。
 *
 * 顺序  区域              起始地址        大小      对应扇区
 * 1     Bootloader        0x08000000      16KB      S0
 * 2     Boot Meta         0x08004000      16KB      S1（数据 meta + 追加升级日志）
 * 3     配置主区          0x08008000      16KB      S2
 * 4     配置备份          0x0800C000      16KB      S3
 * 5     错误日志          0x08010000      64KB      S4（文本追加，带 Unix 时间）
 * 6     APP 主程序        0x08020000      128KB     S5
 * 7     BACKUP 备份       0x08040000      128KB     S6
 * 8     OTA 升级包        0x08060000      128KB     S7
 *
 * 升级流程：
 *   上位机把新固件（按 APP 区地址 0x08020000 链接）经串口 OTA 写入 OTA 区；
 *   Bootloader 启动时若发现 ota 标识：APP -> BACKUP 备份，OTA -> APP，
 *   校验通过后清除标识；App 初始化完成调用 boot_confirm() 确认成功，
 *   否则崩溃/重试超限后用 BACKUP 恢复 APP，运行旧程序。
 *
 * Boot Meta 是 S1 内的启动状态（32B）+ 追加升级历史（16B × N），
 * 状态保存时整区擦除重写，升级过程中只追加历史，掉电安全；
 * 错误日志是 S4 内的文本追加区，每条以 [unix] 开头，写满后整区擦除重写。
 *
 * 修改后需同步：
 *   - apps/f407ve/stm32f407ve.ld（App=0x08020000）/ apps/<board>/bootloader.ld
 *   - board.h 的 BOARD_APP_VECTOR_BASE=0x08020000U
 *   - tools/crash_log.py 的 F4 错误日志转储起始地址 0x08010000（64KB）
 */

#define BOOT_BASE               0x08000000U
#define BOOT_SIZE               (16U * 1024U)

#define BOOT_META_BASE          0x08004000U
#define BOOT_META_SIZE          (16U * 1024U)

#define BOOT_CONFIG_BASE0       0x08008000U
#define BOOT_CONFIG_BASE1       0x0800C000U
#define BOOT_CONFIG_SIZE        (16U * 1024U)

#define BOOT_LOG_BASE           0x08010000U
#define BOOT_LOG_SIZE           (64U * 1024U)

#define BOOT_SLOT_APP_BASE      0x08020000U
#define BOOT_SLOT_BACKUP_BASE   0x08040000U
#define BOOT_SLOT_OTA_BASE      0x08060000U
#define BOOT_SLOT_SIZE          (128U * 1024U)

/* ---- 共享 boot_meta/boot_crash/boot_config 使用的板级参数 ---- */
/* 共享 port_ota 以 BOOT_OTA_* 访问升级包区（本板即 S7 OTA 槽） */
#define BOOT_OTA_BASE           BOOT_SLOT_OTA_BASE
#define BOOT_OTA_SIZE           BOOT_SLOT_SIZE

#define BOOT_SRAM_END           0x20020000U     /* VET6 128KB SRAM 顶端 */

#define BOOT_CRASH_TAG          " slot=APP"     /* 崩溃日志标识运行槽 */

/* ---- Bootloader 板级参数（platforms/stm32/bootloader.c） ---- */
#define BOOT_APP_BASE           BOOT_SLOT_APP_BASE
#define BOOT_APP_SIZE           BOOT_SLOT_SIZE

#define BOOT_BANNER             "f407ve APP/BACKUP/OTA"
#define BOOT_HAS_BACKUP                         /* 支持 APP/BACKUP 备份与恢复 */
#define BOOT_OTA_ERASE_FULL                     /* OTA 区按扇区整块擦除 */
#define BOOT_PROGRESS_STEP      (16U * 1024U)   /* 拷贝进度点间隔 */

#endif /* BOARD_PARTITION_H */
