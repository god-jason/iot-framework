#ifndef APP_FS_H
#define APP_FS_H

/*
 * app_fs.h - STM32F407VET6 外部 Flash 文件系统（littlefs + VFS）
 *
 * 使用 SPI1(PB3/PB4/PB5, AF5) + PB2 片选连接 W25Qxx，
 * 文件系统占用前 512KB，挂载点为 "/"；未格式化时自动格式化。
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 初始化 SPI Flash 并挂载 littlefs。
 * 返回 0 表示成功；-1 表示硬件或文件系统初始化失败。
 */
int app_fs_init(void);

/* 文件系统是否已就绪 */
int app_fs_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_FS_H */
