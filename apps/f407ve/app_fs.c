/*
 * app_fs.c - STM32F407VET6 外部 SPI Flash 文件系统（MFS + VFS）
 * ============================================================================
 *
 * 硬件连接：
 *   SPI1_SCK  = PB3  (AF5)
 *   SPI1_MISO = PB4  (AF5)
 *   SPI1_MOSI = PB5  (AF5)
 *   CS        = PA15
 *
 * 文件系统使用 W25Q16 整片 2MB（FS_PARTITION_SIZE）。
 * 本文件把 MFS 适配为 fs_ops_t 并挂载到 VFS 根目录 "/"；
 * MFS 的 lock/unlock 在 mfs_config_t 中配置，适配层不重复加锁。
 */

#include "app_fs.h"

#include "ros.h"
#include "fs.h"
#include "fs_cmd.h"
#include "mfs.h"
#include "vfs.h"
#include "w25qxx.h"
#include "hal_spi.h"

#include "stm32_cube.h"

#include <string.h>

#define FS_SPI_ID            0U               /* SPI1 */
#define FS_CS_PIN            HAL_PIN(0, 15)   /* PA15 */
#define FS_SECTOR_SIZE       4096U            /* W25Q 扇区大小 */
#define FS_PARTITION_SIZE    (2048U * 1024U)  /* 整片 2MB */
#define FS_SPI_HZ            10000000U

static hal_spi_t       s_fs_spi;
static w25qxx_handle_t s_fs_flash;
static ros_mutex_t     s_fs_lock;
static uint8_t         s_fs_ready;

/* ==========================================================================
 * MFS 适配层：mfs_* -> fs_ops_t
 * ========================================================================== */

typedef struct {
    mfs_t fs;
    mfs_blockdev_t dev;
    mfs_config_t cfg;
    uint8_t workbuf[MFS_WORKBUF_SIZE];
    uint8_t mounted;
    uint8_t auto_format;
} app_mfs_t;

static app_mfs_t s_mfs;

/* MFS 错误码 -> 通用 FS 错误码 */
static int mfs_map_err(int err)
{
    switch (err) {
    case MFS_OK:               return FS_OK;
    case MFS_ERR_PARAM:        return FS_ERR_INVAL;
    case MFS_ERR_IO:           return FS_ERR_IO;
    case MFS_ERR_CRC:          return FS_ERR_CORRUPT;
    case MFS_ERR_FORMAT:       return FS_ERR_FORMAT;
    case MFS_ERR_VERSION:      return FS_ERR_NOTSUP;
    case MFS_ERR_NOENT:        return FS_ERR_NOENT;
    case MFS_ERR_EXIST:        return FS_ERR_EXIST;
    case MFS_ERR_NOTDIR:       return FS_ERR_NOTDIR;
    case MFS_ERR_ISDIR:        return FS_ERR_ISDIR;
    case MFS_ERR_NOSPC:        return FS_ERR_NOSPC;
    case MFS_ERR_NOMEM:        return FS_ERR_NOMEM;
    case MFS_ERR_BUSY:         return FS_ERR_BUSY;
    case MFS_ERR_INVAL:        return FS_ERR_INVAL;
    case MFS_ERR_NOTEMPTY:     return FS_ERR_NOTEMPTY;
    case MFS_ERR_BADF:         return FS_ERR_BADF;
    case MFS_ERR_NOTSUP:       return FS_ERR_NOTSUP;
    case MFS_ERR_STATE:        return FS_ERR_STATE;
    default:                   return FS_ERR_IO;
    }
}

/* 通用打开标志 -> MFS 打开标志 */
static uint32_t mfs_map_flags(uint32_t flags)
{
    uint32_t out = flags & 3u;

    if ((flags & FS_O_CREAT) != 0u) {
        out |= MFS_O_CREAT;
    }
    if ((flags & FS_O_EXCL) != 0u) {
        out |= MFS_O_EXCL;
    }
    if ((flags & FS_O_TRUNC) != 0u) {
        out |= MFS_O_TRUNC;
    }
    if ((flags & FS_O_APPEND) != 0u) {
        out |= MFS_O_APPEND;
    }
    return out;
}

static int32_t mfs_map_io(int32_t n)
{
    return (n < 0) ? mfs_map_err(n) : n;
}

static int mfs_op_format(void *ctx)
{
    app_mfs_t *m = (app_mfs_t *)ctx;
    int err = mfs_format(&m->fs);

    if (err == MFS_OK) {
        m->mounted = 1u;
    }
    return mfs_map_err(err);
}

static int mfs_op_mount(void *ctx)
{
    app_mfs_t *m = (app_mfs_t *)ctx;
    int err = mfs_mount(&m->fs);

    if (err != MFS_OK && m->auto_format != 0u) {
        err = mfs_format(&m->fs);
    }
    if (err == MFS_OK) {
        m->mounted = 1u;
    }
    return mfs_map_err(err);
}

static int mfs_op_unmount(void *ctx)
{
    app_mfs_t *m = (app_mfs_t *)ctx;
    int err = mfs_unmount(&m->fs);

    m->mounted = 0u;
    return mfs_map_err(err);
}

static int mfs_op_statfs(void *ctx, fs_statfs_t *stat)
{
    app_mfs_t *m = (app_mfs_t *)ctx;
    mfs_statfs_t st;
    int err;

    if (stat == NULL) {
        return FS_ERR_INVAL;
    }
    err = mfs_statfs(&m->fs, &st);
    if (err != MFS_OK) {
        return mfs_map_err(err);
    }
    stat->total_bytes = st.total_bytes;
    stat->used_bytes = st.used_bytes;
    stat->free_bytes = st.free_bytes;
    stat->total_blocks = st.total_blocks;
    stat->used_blocks = (st.total_blocks >= st.free_blocks + st.bad_blocks)
                            ? st.total_blocks - st.free_blocks - st.bad_blocks
                            : 0u;
    stat->block_size = st.block_size;
    return FS_OK;
}

static int mfs_op_open(void *ctx, const char *path, uint32_t flags, void **fp)
{
    app_mfs_t *m = (app_mfs_t *)ctx;
    mfs_file_t *file = NULL;
    int err;

    if (path == NULL || fp == NULL) {
        return FS_ERR_INVAL;
    }
    err = mfs_open(&m->fs, path, mfs_map_flags(flags), &file);
    if (err != MFS_OK) {
        return mfs_map_err(err);
    }
    *fp = file;
    return FS_OK;
}

static int mfs_op_close(void *ctx, void *fp)
{
    (void)ctx;
    return mfs_map_err(mfs_close((mfs_file_t *)fp));
}

static int32_t mfs_op_read(void *ctx, void *fp, void *buf, uint32_t size)
{
    (void)ctx;
    return mfs_map_io((int32_t)mfs_read((mfs_file_t *)fp, buf, size));
}

static int32_t mfs_op_write(void *ctx, void *fp, const void *buf,
                            uint32_t size)
{
    (void)ctx;
    return mfs_map_io((int32_t)mfs_write((mfs_file_t *)fp, buf, size));
}

static int mfs_op_seek(void *ctx, void *fp, int32_t offset, int whence)
{
    (void)ctx;
    return mfs_map_err(mfs_seek((mfs_file_t *)fp, offset, whence));
}

static int32_t mfs_op_tell(void *ctx, void *fp)
{
    (void)ctx;
    return (int32_t)mfs_tell((mfs_file_t *)fp);
}

static uint32_t mfs_op_size(void *ctx, void *fp)
{
    (void)ctx;
    return (uint32_t)mfs_size((mfs_file_t *)fp);
}

static int mfs_op_stat(void *ctx, const char *path, fs_stat_t *stat)
{
    app_mfs_t *m = (app_mfs_t *)ctx;
    mfs_stat_t st;
    int err;

    if (path == NULL || stat == NULL) {
        return FS_ERR_INVAL;
    }
    err = mfs_stat(&m->fs, path, &st);
    if (err != MFS_OK) {
        return mfs_map_err(err);
    }
    stat->type = st.type;
    stat->size = st.size;
    strncpy(stat->name, st.name, FS_NAME_MAX);
    stat->name[FS_NAME_MAX] = '\0';
    return FS_OK;
}

static int mfs_op_mkdir(void *ctx, const char *path)
{
    app_mfs_t *m = (app_mfs_t *)ctx;

    return mfs_map_err(mfs_mkdir(&m->fs, path));
}

static int mfs_op_rmdir(void *ctx, const char *path)
{
    app_mfs_t *m = (app_mfs_t *)ctx;

    return mfs_map_err(mfs_rmdir(&m->fs, path));
}

static int mfs_op_unlink(void *ctx, const char *path)
{
    app_mfs_t *m = (app_mfs_t *)ctx;

    return mfs_map_err(mfs_unlink(&m->fs, path));
}

static int mfs_op_rename(void *ctx, const char *oldpath, const char *newpath)
{
    app_mfs_t *m = (app_mfs_t *)ctx;

    return mfs_map_err(mfs_rename(&m->fs, oldpath, newpath));
}

static int mfs_op_opendir(void *ctx, const char *path, void **dp)
{
    app_mfs_t *m = (app_mfs_t *)ctx;
    mfs_dir_t *dir = NULL;
    int err;

    if (path == NULL || dp == NULL) {
        return FS_ERR_INVAL;
    }
    err = mfs_opendir(&m->fs, path, &dir);
    if (err != MFS_OK) {
        return mfs_map_err(err);
    }
    *dp = dir;
    return FS_OK;
}

static int mfs_op_readdir(void *ctx, void *dp, fs_dirent_t *entry)
{
    (void)ctx;
    mfs_dirent_t ent;
    int err;

    if (entry == NULL) {
        return FS_ERR_INVAL;
    }
    err = mfs_readdir((mfs_dir_t *)dp, &ent);
    if (err < 0) {
        return mfs_map_err(err);
    }
    if (err > 0) {
        return 1;                        /* 遍历结束 */
    }
    entry->type = ent.type;
    entry->size = ent.size;
    strncpy(entry->name, ent.name, FS_NAME_MAX);
    entry->name[FS_NAME_MAX] = '\0';
    return 0;
}

static int mfs_op_closedir(void *ctx, void *dp)
{
    (void)ctx;
    return mfs_map_err(mfs_closedir((mfs_dir_t *)dp));
}

static const fs_ops_t s_mfs_ops = {
    .format = mfs_op_format,
    .mount = mfs_op_mount,
    .unmount = mfs_op_unmount,
    .statfs = mfs_op_statfs,
    .open = mfs_op_open,
    .close = mfs_op_close,
    .read = mfs_op_read,
    .write = mfs_op_write,
    .seek = mfs_op_seek,
    .tell = mfs_op_tell,
    .size = mfs_op_size,
    .stat = mfs_op_stat,
    .mkdir = mfs_op_mkdir,
    .rmdir = mfs_op_rmdir,
    .unlink = mfs_op_unlink,
    .rename = mfs_op_rename,
    .opendir = mfs_op_opendir,
    .readdir = mfs_op_readdir,
    .closedir = mfs_op_closedir
};

/* ==========================================================================
 * 硬件与初始化
 * ========================================================================== */

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

/* 块设备回调：锁由 MFS 的 lock/unlock 统一持有 */
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

/* MFS 以块为单位擦除，这里按 4KB 扇区循环调用 W25Q 驱动 */
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
 * 临时：全容量（4MB 目标，写满即止）容量与 GC 回收集成测试
 *   1. 格式化 → 写满直到空间不足（NOSPC）
 *   2. 全文件逐字节校验 → 删除文件
 *   3. 删除后再写 64KB，验证 GC 能回收失效块
 *   4. 输出 df 与总耗时
 * 生产固件默认关闭（APP_FS_FULLTEST 0），需要回归时改为 1。
 * ========================================================================== */
#define APP_FS_FULLTEST  0

#if APP_FS_FULLTEST
#define FULLTEST_PATTERN 0xA5U
/* 目标写入量：4MB；实际在分区写满（NOSPC）时提前结束，即用尽全部空间 */
#define FULLTEST_TOTAL_KB 4096U
/*
 * 每次写入 16KB：MFS 每次 mfs_write 末尾要扫描定位 Inode，
 * 单次写入越大扫描次数越少；但缓冲会占用 RAM，16KB 与 64KB 内核堆
 * 在 128KB SRAM 内比较平衡。
 */
#define FULLTEST_CHUNK   (16U * 1024U)

static uint8_t s_ft_buf[FULLTEST_CHUNK];

/* 顺序读取 len 字节并校验模式；返回 0 成功，负值表示失败位置类别 */
static int ft_read_check(vfs_file_t *f, uint32_t len)
{
    uint32_t off = 0U;

    while (off < len) {
        uint32_t want = len - off;
        int32_t n;
        uint32_t i;

        if (want > 4096U) {
            want = 4096U;
        }
        n = vfs_read(f, s_ft_buf, want);
        if (n <= 0) {
            return -1;
        }
        for (i = 0U; i < (uint32_t)n; i++) {
            if (s_ft_buf[i] != (uint8_t)FULLTEST_PATTERN) {
                return -2;
            }
        }
        off += (uint32_t)n;
    }
    return 0;
}

static void fs_fulltest_run(void)
{
    vfs_file_t f;
    fs_statfs_t st;
    uint32_t total = 0U;
    uint32_t t0 = ros_tick_get();
    int32_t n = 0;
    int rc;

    memset(s_ft_buf, (int)FULLTEST_PATTERN, sizeof(s_ft_buf));

    /* 先格式化，从干净分区开始测满容量；否则残留文件失效块会触发大量 GC，
     * 写满 2MB 会慢到不可接受 */
    rc = vfs_format("/");
    LOG_I("fs", "fulltest: format rc=%d", rc);
    if (rc != FS_OK) {
        return;
    }

    memset(&f, 0, sizeof(f));
    rc = vfs_open("/fill.bin", FS_O_CREAT | FS_O_TRUNC | FS_O_WRONLY, &f);
    if (rc != FS_OK) {
        LOG_E("fs", "fulltest: open failed %d", rc);
        return;
    }

    for (;;) {
        uint32_t t1 = ros_tick_get();

        if (total >= FULLTEST_TOTAL_KB * 1024U) {
            n = 0;
            break;                          /* 达到 4MB 目标 */
        }
        n = vfs_write(&f, s_ft_buf, sizeof(s_ft_buf));
        LOG_I("fs", "ft: %u KB n=%d dt=%u ms", (unsigned)(total / 1024U), (int)n,
              (unsigned)(ros_tick_get() - t1));
        if (n <= 0) {
            break;                          /* 空间不足或 IO 错误 */
        }
        total += (uint32_t)n;
        if ((uint32_t)n < sizeof(s_ft_buf)) {
            break;
        }
    }
    (void)vfs_close(&f);
    LOG_I("fs", "fulltest: wrote %u KB (last rc=%d)", (unsigned)(total / 1024U),
          (int)n);

    if (vfs_statfs("/", &st) == FS_OK) {
        LOG_I("fs", "fulltest: df total=%uKB free=%uKB blocks=%u",
              (unsigned)(st.total_bytes / 1024U),
              (unsigned)(st.free_bytes / 1024U), (unsigned)st.total_blocks);
    }

    /* 回读校验：按实际文件大小取头 4KB + 尾 4KB */
    memset(&f, 0, sizeof(f));
    rc = vfs_open("/fill.bin", FS_O_RDONLY, &f);
    if (rc == FS_OK) {
        uint32_t fsize = vfs_size(&f);
        int head = ft_read_check(&f, FULLTEST_CHUNK);
        int tail = -1;

        if ((fsize > 2U * FULLTEST_CHUNK) &&
            (vfs_seek(&f, (int32_t)(fsize - FULLTEST_CHUNK), FS_SEEK_SET) == FS_OK)) {
            tail = ft_read_check(&f, FULLTEST_CHUNK);
        }
        LOG_I("fs", "fulltest: readback head=%d tail=%d size=%uKB", head, tail,
              (unsigned)(fsize / 1024U));

        /* 全文件校验：报告第一个不一致的字节偏移 */
        {
            int bad = -1;
            uint32_t off = 0U;

            if (vfs_seek(&f, 0, FS_SEEK_SET) == FS_OK) {
                while (off < fsize) {
                    uint32_t want = fsize - off;
                    int32_t rn;
                    uint32_t k;

                    if (want > 4096U) {
                        want = 4096U;
                    }
                    rn = vfs_read(&f, s_ft_buf, want);
                    if (rn <= 0) {
                        bad = (int)off;
                        break;
                    }
                    for (k = 0U; k < (uint32_t)rn; k++) {
                        if (s_ft_buf[k] != (uint8_t)FULLTEST_PATTERN) {
                            bad = (int)(off + k);
                            break;
                        }
                    }
                    if (bad >= 0) {
                        break;
                    }
                    off += (uint32_t)rn;
                }
            }
            LOG_I("fs", "fulltest: verify size=%uKB first_bad=%d",
                  (unsigned)(fsize / 1024U), bad);
        }
        (void)vfs_close(&f);
    } else {
        LOG_E("fs", "fulltest: reopen failed %d", rc);
    }

    LOG_I("fs", "fulltest: unlink=%d cost=%u ms", vfs_unlink("/fill.bin"),
          (unsigned)(ros_tick_get() - t0));

    /* 删除后再次写入 64KB：验证 GC 能回收失效块并重新分配 */
    total = 0U;
    memset(&f, 0, sizeof(f));
    rc = vfs_open("/reuse.bin", FS_O_CREAT | FS_O_TRUNC | FS_O_WRONLY, &f);
    if (rc == FS_OK) {
        while (total < 64U * 1024U) {
            uint32_t t1 = ros_tick_get();

            n = vfs_write(&f, s_ft_buf, sizeof(s_ft_buf));
            LOG_I("fs", "reuse: %u KB n=%d dt=%u ms", (unsigned)(total / 1024U),
                  (int)n, (unsigned)(ros_tick_get() - t1));
            if (n <= 0) {
                break;
            }
            total += (uint32_t)n;
            if ((uint32_t)n < sizeof(s_ft_buf)) {
                break;
            }
        }
        (void)vfs_close(&f);
        LOG_I("fs", "reuse: wrote %u KB (last rc=%d)", (unsigned)(total / 1024U),
              (int)n);
        memset(&f, 0, sizeof(f));
        if (vfs_open("/reuse.bin", FS_O_RDONLY, &f) == FS_OK) {
            LOG_I("fs", "reuse: readback=%d", ft_read_check(&f, FULLTEST_CHUNK));
            (void)vfs_close(&f);
        }
        LOG_I("fs", "reuse: unlink=%d", vfs_unlink("/reuse.bin"));
    } else {
        LOG_E("fs", "reuse: open failed %d", rc);
    }

    if (vfs_statfs("/", &st) == FS_OK) {
        LOG_I("fs", "fulltest: final total=%uKB free=%uKB used_blocks=%u",
              (unsigned)(st.total_bytes / 1024U),
              (unsigned)(st.free_bytes / 1024U), (unsigned)st.used_blocks);
    }
    LOG_I("fs", "fulltest: done cost=%u ms", (unsigned)(ros_tick_get() - t0));
}
#endif /* APP_FS_FULLTEST */

/*
 * 初始化 SPI Flash 并把 MFS 挂载到 VFS 根目录。
 * 未格式化时由 auto_format 自动格式化，成功后注册 FS Shell 命令。
 */
int app_fs_init(void)
{
    hal_spi_config_t spi_cfg;
    fs_statfs_t stat;
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
        LOG_E("fs", "spi init failed: %d", rc);
        return -1;
    }

    rc = w25qxx_init(&s_fs_flash, &s_fs_spi, FS_CS_PIN);
    if (rc != HAL_OK) {
        LOG_E("fs", "spi flash init failed: %d", rc);
        return -1;
    }

    /* 分区取容量与配置值的较小者，并按扇区向下对齐 */
    part_size = s_fs_flash.capacity;
    if (part_size > FS_PARTITION_SIZE) {
        part_size = FS_PARTITION_SIZE;
    }
    part_size = (part_size / FS_SECTOR_SIZE) * FS_SECTOR_SIZE;
    if (part_size < 3u * FS_SECTOR_SIZE) {
        LOG_E("fs", "spi flash capacity invalid");
        return -1;
    }

    memset(&s_mfs, 0, sizeof(s_mfs));
    s_mfs.auto_format = 1u;

    s_mfs.dev.read = fs_flash_read;
    s_mfs.dev.program = fs_flash_program;
    s_mfs.dev.erase = fs_flash_erase;
    s_mfs.dev.sync = fs_flash_sync;
    s_mfs.dev.start_addr = 0u;
    s_mfs.dev.total_size = part_size;
    s_mfs.dev.erase_size = FS_SECTOR_SIZE;
    s_mfs.dev.program_size = 1u;
    s_mfs.dev.read_size = 1u;

    s_mfs.cfg.workbuf = s_mfs.workbuf;
    s_mfs.cfg.workbuf_size = sizeof(s_mfs.workbuf);
    s_mfs.cfg.max_open_files = MFS_MAX_OPEN_FILES;
    s_mfs.cfg.max_open_dirs = MFS_MAX_OPEN_DIRS;
    s_mfs.cfg.auto_gc = 1u;
    s_mfs.cfg.lock = fs_lock;
    s_mfs.cfg.unlock = fs_unlock;

    rc = mfs_map_err(mfs_init(&s_mfs.fs, &s_mfs.dev, &s_mfs.cfg));
    if (rc != FS_OK) {
        LOG_E("fs", "mfs init failed: %d", rc);
        return -1;
    }

    rc = vfs_mount("/", &s_mfs_ops, &s_mfs);
    if (rc != FS_OK) {
        LOG_E("fs", "mount failed: %d", rc);
        return -1;
    }

    if (vfs_statfs("/", &stat) == FS_OK) {
        LOG_I("fs", "mounted: %u KB free / %u KB",
              (unsigned)(stat.free_bytes / 1024U),
              (unsigned)(stat.total_bytes / 1024U));
    } else {
        LOG_I("fs", "mounted");
    }

#if APP_FS_FULLTEST
    fs_fulltest_run();
#endif

    (void)fs_cmd_register();

    s_fs_ready = 1u;
    return 0;
}

int app_fs_ready(void)
{
    return (s_fs_ready != 0u) ? 0 : -1;
}
