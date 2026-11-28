#ifndef BOARD_PARTITION_H
#define BOARD_PARTITION_H

/*
 * partition.h - STM32F103CBT6（128KB Flash）单 App 槽分区
 *
 * 顺序  区域              起始地址        大小
 * 1     Bootloader        0x08000000      8KB
 * 2     KV 区 A           0x08002000      8KB
 * 3     KV 区 B           0x08004000      8KB
 * 4     主程序 App        0x08006000      52KB
 * 5     升级包 OTA        0x08013000      52KB
 *
 * KV 区由 components/fkv 使用（双区滚动日志、掉电安全）：
 *   - A/B 两区轮流追加，写满后把有效记录搬到另一区并擦除旧区；
 *   - 启动状态（tries/crash）、应用配置存放在 KV 中。
 * 本板没有独立错误日志区：崩溃只累计 crash 计数（存 KV），不写文本日志。
 *
 * 升级流程：
 *   1. 串口 OTA 把升级包写入 OTA 区，包格式见 components/fota/fota.h：
 *      32B 包头（魔术字/版本/flags/镜像与负载长度、CRC）+ 原始镜像；
 *   2. Bootloader 每次启动检测 OTA 区包头，有效则擦除 APP 区并写入镜像；
 *   3. 校验通过后擦除 OTA 区；升级中断则包头仍在，下次启动重新升级。
 *
 * 修改后需同步：
 *   - apps/f103cb/stm32f103cb.ld（App=0x08006000，52KB）/ bootloader.ld
 *   - board.h 的 BOARD_APP_VECTOR_BASE=0x08006000U
 */

#define FLASH_ROM_BASE               0x08000000U
#define FLASH_ROM_SIZE               (8U * 1024U)

/* KV 区：A/B 两个等大分区（components/fkv 使用） */
#define FLASH_KV_BASE0           0x08002000U
#define FLASH_KV_BASE1           0x08004000U
#define FLASH_KV_SIZE            (8U * 1024U)

#define FLASH_APP_BASE           0x08006000U
#define FLASH_APP_SIZE           (52U * 1024U)

#define FLASH_OTA_BASE           0x08013000U
#define FLASH_OTA_SIZE           (52U * 1024U)

/* ---- 共享 boot_meta 使用的板级参数 ---- */
#define FLASH_SRAM_END           0x20005000U     /* CBT6 20KB SRAM 顶端 */

#define FLASH_CRASH_TAG          ""              /* 单槽，无 slot 前缀 */

/* ---- Bootloader 板级参数（platforms/stm32/bootloader/bootloader.c） ---- */
#define FLASH_BANNER             "f103cb single-slot OTA"
#define FLASH_PROGRESS_STEP      (8U * 1024U)    /* 写入进度点间隔 */
#define FLASH_OTA_HDR_ERASE      (1U * 1024U)    /* 升级成功后擦 1 页即可去掉包头 */

#endif /* BOARD_PARTITION_H */
