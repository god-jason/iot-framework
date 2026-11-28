#ifndef BOARD_PARTITION_H
#define BOARD_PARTITION_H

/*
 * partition.h - STM32F407VE（512KB Flash）APP/OTA 分区定义
 *
 * F407VE 扇区：4×16KB + 1×64KB + 3×128KB，分区必须扇区对齐。
 *
 * 顺序  区域              起始地址        大小      对应扇区
 * 1     Bootloader        0x08000000      16KB      S0
 * 2     KV 区 A           0x08004000      16KB      S1
 * 3     KV 区 B           0x08008000      16KB      S2
 * 4     错误日志          0x0800C000      16KB      S3（文本追加，带 Unix 时间）
 * 5     APP 主程序        0x08010000      192KB     S4(64K) + S5(128K)
 * 6     OTA 升级包        0x08040000      256KB     S6 + S7
 *
 * KV 区由 src/fkv 使用（双区滚动日志、掉电安全）：
 *   - A/B 两区轮流追加，写满后把有效记录搬到另一区并擦除旧区；
 *   - 启动状态（tries/crash）、应用配置存放在 KV 中。
 *
 * 升级流程（无备份区）：
 *   1. 上位机把升级包写入 OTA 区，包格式见 src/fota/fota.h：
 *      32B 包头（魔术字/版本/flags/镜像与负载长度、CRC）+ 负载；
 *   2. Bootloader 每次启动检测 OTA 区包头，有效则擦除 APP 区并写入镜像
 *      （负载为 DEFLATE 时流式解压，见 FLASH_OTA_COMPRESS）；
 *   3. 校验通过后擦除 OTA 区；升级中断则包头仍在，下次启动重新升级。
 *
 * 修改后需同步：
 *   - apps/f407ve/stm32f407ve.ld（App=0x08010000，192KB）/ bootloader.ld
 *   - board.h 的 BOARD_APP_VECTOR_BASE=0x08010000U
 *   - tools/crash_log.py 的 F4 错误日志转储起始地址 0x0800C000（16KB）
 */

#define FLASH_ROM_BASE               0x08000000U
#define FLASH_ROM_SIZE               (16U * 1024U)

/* KV 区：A/B 两个等大分区（src/fkv 使用） */
#define FLASH_KV_BASE0           0x08004000U
#define FLASH_KV_BASE1           0x08008000U
#define FLASH_KV_SIZE            (16U * 1024U)

#define FLASH_LOG_BASE           0x0800C000U
#define FLASH_LOG_SIZE           (16U * 1024U)

#define FLASH_APP_BASE           0x08010000U
#define FLASH_APP_SIZE           (192U * 1024U)

#define FLASH_OTA_BASE           0x08040000U
#define FLASH_OTA_SIZE           (256U * 1024U)

/* ---- 共享 boot_meta 使用的板级参数 ---- */
#define FLASH_SRAM_END           0x20020000U     /* VET6 128KB SRAM 顶端 */

#define FLASH_CRASH_TAG          " slot=APP"     /* 崩溃日志标识运行槽 */

/* ---- Bootloader 板级参数（platforms/stm32/bootloader/bootloader.c） ---- */
#define FLASH_BANNER             "f407ve APP/OTA"

/*
 * F4 按扇区擦除（最小 16KB），OTA 区整块擦除；
 * FLASH_PROGRESS_STEP 只用于 Bootloader 打印进度点。
 */
#define FLASH_OTA_ERASE_FULL                     /* OTA 区按扇区整块擦除 */
#define FLASH_PROGRESS_STEP      (16U * 1024U)   /* 写入进度点间隔 */

/*
 * Bootloader 支持 DEFLATE 压缩升级包（负载为 raw deflate 时流式解压到
 * APP 区）。解压需要 32KB 回退窗口 + Huffman 表，F407 的 128KB SRAM 足够；
 * F103（20KB SRAM / 8KB Bootloader Flash）不启用。
 */
#define FLASH_OTA_COMPRESS

#endif /* BOARD_PARTITION_H */
