/*
 * app_fs.c - STM32F407VET6 外部 SPI Flash 文件系统（ROFS）
 * ============================================================================
 *
 * 硬件连接：
 *   SPI1_SCK  = PB3  (AF5)
 *   SPI1_MISO = PB4  (AF5)
 *   SPI1_MOSI = PB5  (AF5)
 *   CS        = PB2
 *
 * 注：PA6/PA7 被板载 LED 占用，因此 SPI1 使用 PB3/PB4/PB5 复用引脚。
 *
 * 文件系统使用 W25Q 的前 512KB（可改 FS_PARTITION_SIZE），
 * 挂载失败时自动格式化；格式化会擦除 128 个 4KB 扇区。
 *
 * 并发：SPI Flash 的串行访问由 ROFS 的 lock/unlock 统一保护，
 *      块设备回调内部不再重复加锁，避免非递归互斥量死锁。
 */

#include "app_fs.h"

#include "ros.h"
#include "roshell.h"
#include "rofs.h"
#include "w25qxx.h"
#include "hal_spi.h"

#include "stm32_cube.h"

#include <string.h>

#define FS_SPI_ID            0U               /* SPI1 */
#define FS_CS_PIN            HAL_PIN(1, 2)    /* PB2 */
#define FS_SECTOR_SIZE       4096U            /* W25Q 扇区大小 */
#define FS_PARTITION_SIZE    (512U * 1024U)   /* 使用前 512KB，缩短格式/扫描时间 */
#define FS_SPI_HZ            10000000U

static hal_spi_t       s_fs_spi;
static w25qxx_handle_t s_fs_flash;
static ros_mutex_t     s_fs_lock;
static rofs_t          s_rofs;
static uint8_t         s_rofs_work[ROFS_WORKBUF_SIZE];
static uint8_t         s_rofs_ready;

/* Cube SPI 初始化钩子：SPI1 复用 PB3(SCK)/PB4(MISO)/PB5(MOSI) */
void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    GPIO_InitTypeDef gpio;

    if (hspi->Instance == SPI1) {
        __HAL_RCC_SPI1_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();

        memset(&gpio, 0, sizeof(gpio));
        gpio.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio.Alternate = GPIO_AF5_SPI1;
        HAL_GPIO_Init(GPIOB, &gpio);
    }
}

/* ROFS 块设备回调：锁由 rofs_config_t.lock 在文件系统层统一持有 */
static int fs_flash_read(void *ctx, uint32_t addr, void *buffer, uint32_t size)
{
    (void)ctx;
    if (w25qxx_read(&s_fs_flash, addr, buffer, (size_t)size) != HAL_OK) {
        return -1;
    }
    return 0;
}

static int fs_flash_program(void *ctx, uint32_t addr,
                            const void *buffer, uint32_t size)
{
    (void)ctx;
    if (w25qxx_write(&s_fs_flash, addr, buffer, (size_t)size) != HAL_OK) {
        return -1;
    }
    return 0;
}

/* ROFS 以块为单位擦除，这里按 4KB 扇区循环调用 W25Q 驱动 */
static int fs_flash_erase(void *ctx, uint32_t addr, uint32_t size)
{
    uint32_t off;

    (void)ctx;
    for (off = 0u; off < size; off += FS_SECTOR_SIZE) {
        if (w25qxx_erase_sector(&s_fs_flash, addr + off) != HAL_OK) {
            return -1;
        }
    }
    return 0;
}

static int fs_flash_sync(void *ctx)
{
    (void)ctx;
    return 0;                           /* W25Q 写入为同步等待完成 */
}

static void fs_lock(void *ctx)
{
    (void)ctx;
    (void)ros_mutex_lock(&s_fs_lock, ROS_TIMEOUT_FOREVER);
}

static void fs_unlock(void *ctx)
{
    (void)ctx;
    (void)ros_mutex_unlock(&s_fs_lock);
}

/* ==========================================================================
 * Shell 命令：ls / cat / mkdir / rm / rmdir / stat / df / put
 * ========================================================================== */

/* 取文件系统句柄；未初始化时打印提示并返回 NULL */
static rofs_t *cmd_fs(void)
{
    rofs_t *fs = app_fs_handle();

    if (fs == NULL) {
        ros_puts("fs: not ready");
    }
    return fs;
}

/* ls [path]：列出目录内容，默认根目录 */
static int cmd_ls(const rs_cmd_ctx_t *ctx)
{
    rofs_t *fs = cmd_fs();
    rofs_dir_t *dir = NULL;
    rofs_dirent_t ent;
    const char *path = NULL;
    int rc;

    if (fs == NULL) {
        return -1;
    }
    (void)rs_args_get_str(ctx->parsed, 0U, &path);
    if (path == NULL || path[0] == '\0') {
        path = "/";
    }

    rc = rofs_opendir(fs, path, &dir);
    if (rc != ROFS_OK) {
        ros_printf("ls: %s: %s\n", path, rofs_strerror(rc));
        return -1;
    }

    for (;;) {
        rc = rofs_readdir(dir, &ent);
        if (rc == 1) {
            break;                       /* 遍历结束 */
        }
        if (rc < 0) {
            ros_printf("ls: %s\n", rofs_strerror(rc));
            break;
        }
        ros_printf("%c %8u  %s\n", (ent.type == 2U) ? 'd' : '-',
                   (unsigned)ent.size, ent.name);
    }

    (void)rofs_closedir(dir);
    return 0;
}

/* cat <path>：按原始字节输出文件内容 */
static int cmd_cat(const rs_cmd_ctx_t *ctx)
{
    rofs_t *fs = cmd_fs();
    rofs_file_t *f = NULL;
    const char *path = NULL;
    uint8_t buf[128];
    int n;
    int rc;

    if (fs == NULL) {
        return -1;
    }
    if (!rs_args_get_str(ctx->parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: cat <path>");
        return -1;
    }

    rc = rofs_open(fs, path, ROFS_O_RDONLY, &f);
    if (rc != ROFS_OK) {
        ros_printf("cat: %s: %s\n", path, rofs_strerror(rc));
        return -1;
    }

    while ((n = rofs_read(f, buf, sizeof(buf))) > 0) {
        (void)ros_io_write(NULL, buf, (uint32_t)n);
    }
    if (n < 0) {
        ros_printf("\ncat: %s\n", rofs_strerror(n));
    }
    (void)rofs_close(f);
    return (n < 0) ? -1 : 0;
}

/* mkdir <path> */
static int cmd_mkdir(const rs_cmd_ctx_t *ctx)
{
    rofs_t *fs = cmd_fs();
    const char *path = NULL;
    int rc;

    if (fs == NULL) {
        return -1;
    }
    if (!rs_args_get_str(ctx->parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: mkdir <path>");
        return -1;
    }
    rc = rofs_mkdir(fs, path);
    if (rc != ROFS_OK) {
        ros_printf("mkdir: %s: %s\n", path, rofs_strerror(rc));
        return -1;
    }
    return 0;
}

/* rm <path>：删除文件 */
static int cmd_rm(const rs_cmd_ctx_t *ctx)
{
    rofs_t *fs = cmd_fs();
    const char *path = NULL;
    int rc;

    if (fs == NULL) {
        return -1;
    }
    if (!rs_args_get_str(ctx->parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: rm <path>");
        return -1;
    }
    rc = rofs_unlink(fs, path);
    if (rc == ROFS_ERR_ISDIR) {
        ros_printf("rm: %s: is a directory (use rmdir)\n", path);
        return -1;
    }
    if (rc != ROFS_OK) {
        ros_printf("rm: %s: %s\n", path, rofs_strerror(rc));
        return -1;
    }
    return 0;
}

/* rmdir <path>：删除空目录 */
static int cmd_rmdir(const rs_cmd_ctx_t *ctx)
{
    rofs_t *fs = cmd_fs();
    const char *path = NULL;
    int rc;

    if (fs == NULL) {
        return -1;
    }
    if (!rs_args_get_str(ctx->parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: rmdir <path>");
        return -1;
    }
    rc = rofs_rmdir(fs, path);
    if (rc != ROFS_OK) {
        ros_printf("rmdir: %s: %s\n", path, rofs_strerror(rc));
        return -1;
    }
    return 0;
}

/* stat <path>：显示属性 */
static int cmd_stat(const rs_cmd_ctx_t *ctx)
{
    rofs_t *fs = cmd_fs();
    rofs_stat_t st;
    const char *path = NULL;
    int rc;

    if (fs == NULL) {
        return -1;
    }
    if (!rs_args_get_str(ctx->parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: stat <path>");
        return -1;
    }
    rc = rofs_stat(fs, path, &st);
    if (rc != ROFS_OK) {
        ros_printf("stat: %s: %s\n", path, rofs_strerror(rc));
        return -1;
    }
    ros_printf("%s: inode=%u parent=%u type=%s size=%u version=%u\n",
               st.name, (unsigned)st.inode, (unsigned)st.parent,
               (st.type == 2U) ? "dir" : "file",
               (unsigned)st.size, (unsigned)st.version);
    return 0;
}

/* df：显示空间统计 */
static int cmd_df(const rs_cmd_ctx_t *ctx)
{
    rofs_t *fs = cmd_fs();
    rofs_statfs_t st;
    int rc;

    (void)ctx;
    if (fs == NULL) {
        return -1;
    }
    rc = rofs_statfs(fs, &st);
    if (rc != ROFS_OK) {
        ros_printf("df: %s\n", rofs_strerror(rc));
        return -1;
    }
    ros_printf("total=%uKB used=%uKB free=%uKB blocks=%u free_blocks=%u\n",
               (unsigned)(st.total_bytes / 1024U),
               (unsigned)(st.used_bytes / 1024U),
               (unsigned)(st.free_bytes / 1024U),
               (unsigned)st.total_blocks, (unsigned)st.free_blocks);
    return 0;
}

/* put <path> <text>：创建/覆盖文件（便于在 Shell 中验证读写） */
static int cmd_put(const rs_cmd_ctx_t *ctx)
{
    rofs_t *fs = cmd_fs();
    rofs_file_t *f = NULL;
    const char *path = NULL;
    const char *text = NULL;
    uint32_t len;
    int rc;

    if (fs == NULL) {
        return -1;
    }
    if (!rs_args_get_str(ctx->parsed, 0U, &path) ||
        !rs_args_get_str(ctx->parsed, 1U, &text) ||
        path == NULL || text == NULL) {
        ros_puts("usage: put <path> <text>");
        return -1;
    }

    len = (uint32_t)strlen(text);
    rc = rofs_open(fs, path, ROFS_O_CREAT | ROFS_O_TRUNC | ROFS_O_WRONLY, &f);
    if (rc != ROFS_OK) {
        ros_printf("put: %s: %s\n", path, rofs_strerror(rc));
        return -1;
    }
    rc = (int)rofs_write(f, text, len);
    (void)rofs_close(f);
    if (rc != (int)len) {
        ros_printf("put: %s: write failed\n", path);
        return -1;
    }
    ros_printf("put: %u bytes -> %s\n", (unsigned)len, path);
    return 0;
}

static const rs_args_t s_ls_args[] = {
    { "path", 0, RS_ARGS_TYPE_STR, true, false, "/", "directory (default /)" }
};

static const rs_args_t s_path_arg[] = {
    { "path", 0, RS_ARGS_TYPE_STR, true, true, NULL, "path" }
};

static const rs_args_t s_put_args[] = {
    { "path", 0, RS_ARGS_TYPE_STR, true, true, NULL, "file path" },
    { "text", 0, RS_ARGS_TYPE_STR, true, true, NULL, "text" }
};

static const rs_cmd_t s_ls_def = {
    "ls", "list ROFS directory [path]", NULL, NULL,
    s_ls_args, 1U, cmd_ls, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_cat_def = {
    "cat", "print file contents <path>", NULL, NULL,
    s_path_arg, 1U, cmd_cat, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_mkdir_def = {
    "mkdir", "create directory <path>", NULL, NULL,
    s_path_arg, 1U, cmd_mkdir, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_rm_def = {
    "rm", "remove file <path>", NULL, NULL,
    s_path_arg, 1U, cmd_rm, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_rmdir_def = {
    "rmdir", "remove empty directory <path>", NULL, NULL,
    s_path_arg, 1U, cmd_rmdir, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_stat_def = {
    "stat", "show ROFS entry attributes <path>", NULL, NULL,
    s_path_arg, 1U, cmd_stat, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_df_def = {
    "df", "show ROFS space usage", NULL, NULL,
    NULL, 0U, cmd_df, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_put_def = {
    "put", "create/overwrite file <path> <text>", NULL, NULL,
    s_put_args, 2U, cmd_put, NULL, NULL, NULL, NULL
};

/* 注册文件系统命令；在挂载成功后调用 */
static void app_fs_register_cmds(void)
{
    (void)rs_cmd_register(&s_ls_def);
    (void)rs_cmd_register(&s_cat_def);
    (void)rs_cmd_register(&s_mkdir_def);
    (void)rs_cmd_register(&s_rm_def);
    (void)rs_cmd_register(&s_rmdir_def);
    (void)rs_cmd_register(&s_stat_def);
    (void)rs_cmd_register(&s_df_def);
    (void)rs_cmd_register(&s_put_def);
}

/*
 * 初始化 SPI Flash 并挂载 ROFS。
 * 流程：SPI1 初始化 -> W25Q 探测 -> 计算分区 -> rofs_init -> mount（失败则 format），
 * 挂载成功后注册 ls/cat/mkdir 等命令。
 * 返回 0 成功，-1 失败。
 */
int app_fs_init(void)
{
    hal_spi_config_t spi_cfg;
    rofs_blockdev_t dev;
    rofs_config_t cfg;
    rofs_statfs_t stat;
    uint32_t part_size;
    int rc;

    (void)ros_mutex_init(&s_fs_lock);

    memset(&spi_cfg, 0, sizeof(spi_cfg));
    spi_cfg.id = FS_SPI_ID;
    spi_cfg.mode = HAL_SPI_MODE_MASTER;
    spi_cfg.direction = HAL_SPI_DIRECTION_2LINES;
    spi_cfg.baudrate = FS_SPI_HZ;
    spi_cfg.cpol = HAL_SPI_CPOL_LOW;
    spi_cfg.cpha = HAL_SPI_CPHA_1EDGE;
    spi_cfg.bit_order = HAL_SPI_BITORDER_MSB;
    spi_cfg.data_bits = 8U;
    rc = hal_spi_init(&s_fs_spi, &spi_cfg);
    if (rc != HAL_OK) {
        LOG_E("rofs", "spi init failed: %d", rc);
        return -1;
    }

    rc = w25qxx_init(&s_fs_flash, &s_fs_spi, FS_CS_PIN);
    if (rc != HAL_OK) {
        LOG_E("rofs", "spi flash init failed: %d", rc);
        return -1;
    }

    /* 分区取容量与配置值的较小者，并按扇区向下对齐 */
    part_size = s_fs_flash.capacity;
    if (part_size > FS_PARTITION_SIZE) {
        part_size = FS_PARTITION_SIZE;
    }
    part_size = (part_size / FS_SECTOR_SIZE) * FS_SECTOR_SIZE;
    if (part_size < 3u * FS_SECTOR_SIZE) {
        LOG_E("rofs", "spi flash capacity invalid");
        return -1;
    }

    memset(&dev, 0, sizeof(dev));
    dev.read = fs_flash_read;
    dev.program = fs_flash_program;
    dev.erase = fs_flash_erase;
    dev.sync = fs_flash_sync;
    dev.start_addr = 0u;
    dev.total_size = part_size;
    dev.erase_size = FS_SECTOR_SIZE;
    dev.program_size = 1u;
    dev.read_size = 1u;

    memset(&cfg, 0, sizeof(cfg));
    cfg.workbuf = s_rofs_work;
    cfg.workbuf_size = sizeof(s_rofs_work);
    cfg.max_open_files = ROFS_MAX_OPEN_FILES;
    cfg.max_open_dirs = ROFS_MAX_OPEN_DIRS;
    cfg.lock = fs_lock;
    cfg.unlock = fs_unlock;

    rc = rofs_init(&s_rofs, &dev, &cfg);
    if (rc != ROFS_OK) {
        LOG_E("rofs", "init failed: %d", rc);
        return -1;
    }

    /* 未格式化或 Superblock 不可用时自动格式化一次 */
    rc = rofs_mount(&s_rofs);
    if (rc != ROFS_OK) {
        LOG_I("rofs", "mount failed: %d, formatting", rc);
        rc = rofs_format(&s_rofs);
    }
    if (rc != ROFS_OK) {
        LOG_E("rofs", "format failed: %d", rc);
        return -1;
    }

    if (rofs_statfs(&s_rofs, &stat) == ROFS_OK) {
        LOG_I("rofs", "mounted: %u KB free / %u KB",
              (unsigned)(stat.free_bytes / 1024U),
              (unsigned)(stat.total_bytes / 1024U));
    } else {
        LOG_I("rofs", "mounted");
    }

    app_fs_register_cmds();
    s_rofs_ready = 1u;
    return 0;
}

rofs_t *app_fs_handle(void)
{
    return (s_rofs_ready != 0u) ? &s_rofs : NULL;
}
