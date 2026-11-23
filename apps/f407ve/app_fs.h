#ifndef APP_FS_H
#define APP_FS_H

/*
 * app_fs.h - STM32F407VET6 外部 Flash 文件系统（ROFS）
 *
 * 使用 SPI1(PB3/PB4/PB5, AF5) + PB2 片选连接 W25Qxx，
 * 文件系统占用前 512KB，未格式化时自动格式化。
 */

#include "rofs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 初始化 SPI Flash 并挂载 ROFS。
 * 返回 0 表示成功；-1 表示硬件或文件系统初始化失败。
 */
int app_fs_init(void);

/* 获取 ROFS 实例；初始化失败返回 NULL */
rofs_t *app_fs_handle(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_FS_H */
