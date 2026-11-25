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
 * KV 区由 components/kvfs 使用（双区滚动日志、掉电安全）：
 *   - A/B 两区轮流追加，写满后把有效记录搬到另一区并擦除旧区；
 *   - 启动状态（tries/crash/ota 标识）、OTA 进度、应用配置都存放在 KV 中。
 * 本板没有独立错误日志区：崩溃只累计 crash 计数（存 KV），不写文本日志。
 *
 * 升级流程：
 *   1. 串口 OTA 把新固件（按 App 区 0x08006000 链接）写入升级包区；
 *   2. 接收完成并校验 CRC 后，把 ota 标识/长度/CRC 写入 KV 状态，随后复位；
 *   3. Bootloader 下次启动擦除 App 区，按 1KB 分块把升级包拷入 App 区，
 *      每拷完一块把进度写入 KV（掉电后从最后进度继续）；
 *   4. 全部写完并整段 CRC 校验通过后清除 ota 标识，然后启动 App。
 *
 * 修改后需同步：
 *   - apps/f103cb/stm32f103cb.ld（App=0x08006000，52KB）/ bootloader.ld
 *   - board.h 的 BOARD_APP_VECTOR_BASE=0x08006000U
 */

#define FLASH_ROM_BASE               0x08000000U
#define FLASH_ROM_SIZE               (8U * 1024U)

/* KV 区：A/B 两个等大分区（components/kvfs 使用） */
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
#define FLASH_PROGRESS_STEP      (8U * 1024U)    /* 拷贝进度点间隔 */

#endif /* BOARD_PARTITION_H */
