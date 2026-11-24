#ifndef APP_FS_H
#define APP_FS_H

/*
 * app_fs.h - PC Shell 的文件系统镜像
 *
 * 把 MFS 的块设备后端映射到一个独立的 img 文件，方便在 PC 上：
 *   - 观察 MFS 落盘格式；
 *   - 用外部工具直接查看/拷贝镜像；
 *   - 将同一镜像烧写到板载 SPI Flash 的同一分区。
 *
 * 默认镜像：当前工作目录下的 mfs.img，大小 512KB、4KB 扇区。
 * 上层通过 VFS 访问，挂载点为 "/"。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 默认镜像路径与大小 */
#define APP_FS_IMG_PATH   "mfs.img"
#define APP_FS_IMG_SIZE   (512U * 1024U)

/*
 * 打开/创建镜像并把 MFS 挂载到 VFS 根目录（未格式化时自动格式化）。
 * img_path 为 NULL 时使用 APP_FS_IMG_PATH。
 * 返回 0 成功，-1 失败。
 */
int app_fs_init(const char *img_path);

/* 卸载文件系统并关闭镜像文件 */
void app_fs_deinit(void);

/* 文件系统是否已就绪 */
int app_fs_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_FS_H */
