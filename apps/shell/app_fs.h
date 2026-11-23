#ifndef APP_FS_H
#define APP_FS_H

/*
 * app_fs.h - PC Shell 的文件系统镜像
 *
 * 把 ROFS 的块设备后端映射到一个独立的 img 文件，方便在 PC 上：
 *   - 观察 ROFS 落盘格式；
 *   - 用外部工具直接查看/拷贝镜像；
 *   - 将同一镜像烧写到板载 SPI Flash 的同一分区。
 *
 * 默认镜像：当前工作目录下的 rofs.img，大小 512KB、4KB 扇区，
 * 与 f407ve 的 ROFS 分区参数一致。
 */

#include "rofs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 默认镜像路径与大小 */
#define APP_FS_IMG_PATH   "rofs.img"
#define APP_FS_IMG_SIZE   (512U * 1024U)

/*
 * 打开/创建镜像并挂载 ROFS。
 * img_path 为 NULL 时使用 APP_FS_IMG_PATH。
 * 镜像不存在会自动创建；无有效 Superblock 时自动格式化。
 * 返回 0 成功，-1 失败。
 */
int app_fs_init(const char *img_path);

/* 卸载 ROFS 并关闭镜像文件 */
void app_fs_deinit(void);

/* 获取 ROFS 实例，供上层命令/应用直接调用文件接口 */
rofs_t *app_fs_handle(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_FS_H */
