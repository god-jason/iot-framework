/*
 * rofs_test.c - RAM 块设备上的 ROFS 单元测试
 * ============================================================================
 *
 * 用 64KB 内存数组模拟 SPI NOR：
 *   - 16 个 4KB 块；
 *   - program 只允许 1 -> 0；
 *   - erase 将整块恢复为 0xFF。
 *
 * 覆盖：
 *   - 格式化、挂载、根目录；
 *   - 文件创建、顺序读写、只读打开、SEEK_END；
 *   - 追加、随机覆盖、截断；
 *   - 跨 Chunk 大文件；
 *   - 截断后扩展的空洞必须为 0；
 *   - 重命名、stat、目录遍历、statfs；
 *   - 卸载后重新挂载的数据持久化；
 *   - 删除文件/目录。
 */

#include "test.h"
#include "rofs.h"

#include <string.h>

#define T_BLOCK_SIZE   4096u                   /* 模拟块大小 */
#define T_BLOCK_COUNT  16u                     /* 16 块 = 64KB */
#define T_DISK_SIZE    (T_BLOCK_SIZE * T_BLOCK_COUNT)

static uint8_t s_disk[T_DISK_SIZE];            /* 模拟 Flash */

/* 读取：直接内存拷贝，越界返回错误 */
static int test_read(void *ctx, uint32_t addr, void *buf, uint32_t size)
{
    (void)ctx;
    if (addr > T_DISK_SIZE || size > T_DISK_SIZE - addr) {
        return -1;
    }
    memcpy(buf, &s_disk[addr], size);
    return 0;
}

/* 编程：模拟 NOR 只能把 1 写成 0，不允许 0 -> 1 */
static int test_program(void *ctx, uint32_t addr, const void *buf, uint32_t size)
{
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t i;

    (void)ctx;
    if (addr > T_DISK_SIZE || size > T_DISK_SIZE - addr) {
        return -1;
    }
    for (i = 0; i < size; i++) {
        s_disk[addr + i] &= src[i];       /* 只允许 1 -> 0 */
    }
    return 0;
}

/* 擦除：要求按整块对齐，恢复为 0xFF */
static int test_erase(void *ctx, uint32_t addr, uint32_t size)
{
    (void)ctx;
    if (addr > T_DISK_SIZE || size > T_DISK_SIZE - addr ||
        (addr % T_BLOCK_SIZE) != 0u || (size % T_BLOCK_SIZE) != 0u) {
        return -1;
    }
    memset(&s_disk[addr], 0xFF, size);
    return 0;
}

/* 同步：内存设备无需处理 */
static int test_sync(void *ctx)
{
    (void)ctx;
    return 0;
}

/* 打开并一次性读完整个文件，返回字节数或负错误码 */
static int read_all(rofs_t *fs, const char *path, void *buf, uint32_t cap)
{
    rofs_file_t *f = NULL;
    int n;
    int err;

    err = rofs_open(fs, path, ROFS_O_RDONLY, &f);
    if (err != ROFS_OK) {
        return err;
    }
    n = rofs_read(f, buf, cap);
    (void)rofs_close(f);
    return n;
}

void ut_rofs(void)
{
    static rofs_t fs;                          /* 静态实例，模拟嵌入式用法 */
    static uint8_t workbuf[ROFS_WORKBUF_SIZE]; /* 必需工作缓冲区 */
    rofs_blockdev_t dev;
    rofs_config_t cfg;
    rofs_file_t *f = NULL;
    rofs_dir_t *d = NULL;
    rofs_dirent_t entry;
    rofs_stat_t st;
    rofs_statfs_t sfs;
    char buf[32];
    uint8_t big_w[600];
    uint8_t big_r[600];
    uint32_t i;
    int n;

    /* 组装模拟块设备 */
    memset(&dev, 0, sizeof(dev));
    dev.read = test_read;
    dev.program = test_program;
    dev.erase = test_erase;
    dev.sync = test_sync;
    dev.start_addr = 0u;
    dev.total_size = T_DISK_SIZE;
    dev.erase_size = T_BLOCK_SIZE;
    dev.program_size = 1u;
    dev.read_size = 1u;

    /* ROFS 配置：工作缓冲必需，句柄数量取编译期上限 */
    memset(&cfg, 0, sizeof(cfg));
    cfg.workbuf = workbuf;
    cfg.workbuf_size = sizeof(workbuf);
    cfg.max_open_files = ROFS_MAX_OPEN_FILES;
    cfg.max_open_dirs = ROFS_MAX_OPEN_DIRS;

    memset(s_disk, 0xFF, sizeof(s_disk));

    /* ---------------- 格式化与挂载 ---------------- */
    tf_begin("rofs/format-mount");
    TF_CHECK_INT(rofs_init(&fs, &dev, &cfg), ROFS_OK);
    TF_CHECK_INT(rofs_format(&fs), ROFS_OK);
    TF_CHECK_INT(rofs_isdir(&fs, "/"), 1);
    TF_CHECK_INT(rofs_mkdir(&fs, "/data"), ROFS_OK);
    TF_CHECK_INT(rofs_mkdir(&fs, "/logs"), ROFS_OK);
    TF_CHECK_INT(rofs_isdir(&fs, "/data"), 1);
    TF_CHECK_INT(rofs_mkdir(&fs, "/data"), ROFS_ERR_EXIST);
    tf_end();

    /* ---------------- 基础写读 ---------------- */
    tf_begin("rofs/file-write-read");
    TF_CHECK_INT(rofs_open(&fs, "/data/a.txt",
                           ROFS_O_CREAT | ROFS_O_RDWR, &f), ROFS_OK);
    TF_CHECK(f != NULL);
    TF_CHECK_INT(rofs_write(f, "hello world", 11), 11);
    TF_CHECK_INT(rofs_tell(f), 11);
    TF_CHECK_INT(rofs_size(f), 11);
    TF_CHECK_INT(rofs_seek(f, 0, ROFS_SEEK_SET), ROFS_OK);
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(rofs_read(f, buf, 11), 11);
    TF_CHECK_STR(buf, "hello world");
    TF_CHECK_INT(rofs_close(f), ROFS_OK);
    tf_end();

    /* ---------------- 只读打开与 SEEK_END ---------------- */
    tf_begin("rofs/file-readonly-seek");
    memset(buf, 0, sizeof(buf));
    n = read_all(&fs, "/data/a.txt", buf, sizeof(buf));
    TF_CHECK_INT(n, 11);
    TF_CHECK_STR(buf, "hello world");

    TF_CHECK_INT(rofs_open(&fs, "/data/a.txt", ROFS_O_RDONLY, &f), ROFS_OK);
    TF_CHECK_INT(rofs_seek(f, -3, ROFS_SEEK_END), ROFS_OK);
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(rofs_read(f, buf, 3), 3);
    TF_CHECK_STR(buf, "rld");
    TF_CHECK_INT(rofs_close(f), ROFS_OK);
    tf_end();

    /* ---------------- 追加、随机写、截断 ---------------- */
    tf_begin("rofs/append-random-truncate");
    TF_CHECK_INT(rofs_open(&fs, "/data/a.txt",
                           ROFS_O_WRONLY | ROFS_O_APPEND, &f), ROFS_OK);
    TF_CHECK_INT(rofs_write(f, "!", 1), 1);
    TF_CHECK_INT(rofs_close(f), ROFS_OK);

    /* 在偏移 6 覆盖 "ROFS"，其余字节保持不变 */
    TF_CHECK_INT(rofs_open(&fs, "/data/a.txt", ROFS_O_RDWR, &f), ROFS_OK);
    TF_CHECK_INT(rofs_seek(f, 6, ROFS_SEEK_SET), ROFS_OK);
    TF_CHECK_INT(rofs_write(f, "ROFS", 4), 4);
    TF_CHECK_INT(rofs_close(f), ROFS_OK);

    memset(buf, 0, sizeof(buf));
    n = read_all(&fs, "/data/a.txt", buf, sizeof(buf));
    TF_CHECK_INT(n, 12);
    TF_CHECK_STR(buf, "hello ROFSd!");

    /* 截断到 5，只保留 "hello" */
    TF_CHECK_INT(rofs_open(&fs, "/data/a.txt", ROFS_O_WRONLY, &f), ROFS_OK);
    TF_CHECK_INT(rofs_truncate(f, 5), ROFS_OK);
    TF_CHECK_INT(rofs_close(f), ROFS_OK);

    memset(buf, 0, sizeof(buf));
    n = read_all(&fs, "/data/a.txt", buf, sizeof(buf));
    TF_CHECK_INT(n, 5);
    TF_CHECK_STR(buf, "hello");
    tf_end();

    /* ---------------- 跨 Chunk 文件（600B = 3 个 Chunk） ---------------- */
    tf_begin("rofs/multi-chunk");
    for (i = 0; i < sizeof(big_w); i++) {
        big_w[i] = (uint8_t)(i * 7u + 3u);
    }
    TF_CHECK_INT(rofs_open(&fs, "/big.bin",
                           ROFS_O_CREAT | ROFS_O_RDWR, &f), ROFS_OK);
    TF_CHECK_INT(rofs_write(f, big_w, sizeof(big_w)), (int)sizeof(big_w));
    TF_CHECK_INT(rofs_size(f), (int)sizeof(big_w));
    TF_CHECK_INT(rofs_close(f), ROFS_OK);

    memset(big_r, 0, sizeof(big_r));
    n = read_all(&fs, "/big.bin", big_r, sizeof(big_r));
    TF_CHECK_INT(n, (int)sizeof(big_r));
    TF_CHECK_MEM(big_r, big_w, sizeof(big_w));
    tf_end();

    /* ---------------- 截断后扩展：100..299 必须是空洞 0 ---------------- */
    tf_begin("rofs/truncate-hole");
    TF_CHECK_INT(rofs_open(&fs, "/big.bin", ROFS_O_RDWR, &f), ROFS_OK);
    TF_CHECK_INT(rofs_truncate(f, 100), ROFS_OK);
    TF_CHECK_INT(rofs_close(f), ROFS_OK);
    TF_CHECK_INT(rofs_open(&fs, "/big.bin", ROFS_O_RDWR, &f), ROFS_OK);
    TF_CHECK_INT(rofs_seek(f, 300, ROFS_SEEK_SET), ROFS_OK);
    TF_CHECK_INT(rofs_write(f, "X", 1), 1);
    TF_CHECK_INT(rofs_close(f), ROFS_OK);

    memset(big_r, 0xAA, sizeof(big_r));
    n = read_all(&fs, "/big.bin", big_r, sizeof(big_r));
    TF_CHECK_INT(n, 301);
    TF_CHECK_MEM(big_r, big_w, 100);           /* 前 100 字节保持原数据 */
    {
        int zero_ok = 1;
        for (i = 100; i < 300; i++) {
            if (big_r[i] != 0u) {
                zero_ok = 0;
            }
        }
        TF_CHECK(zero_ok);
    }
    TF_CHECK_INT(big_r[300], 'X');
    tf_end();

    /* ---------------- 重命名与 stat ---------------- */
    tf_begin("rofs/rename-stat");
    TF_CHECK_INT(rofs_rename(&fs, "/data/a.txt", "/data/b.txt"), ROFS_OK);
    TF_CHECK_INT(rofs_exists(&fs, "/data/a.txt"), 0);
    TF_CHECK_INT(rofs_exists(&fs, "/data/b.txt"), 1);
    TF_CHECK_INT(rofs_stat(&fs, "/data/b.txt", &st), ROFS_OK);
    TF_CHECK_STR(st.name, "b.txt");
    TF_CHECK_INT(st.size, 5);
    TF_CHECK_INT(st.type, 1);
    tf_end();

    /* ---------------- 目录遍历 ---------------- */
    tf_begin("rofs/readdir");
    TF_CHECK_INT(rofs_opendir(&fs, "/data", &d), ROFS_OK);
    TF_CHECK(d != NULL);
    memset(&entry, 0, sizeof(entry));
    TF_CHECK_INT(rofs_readdir(d, &entry), 0);
    TF_CHECK_STR(entry.name, "b.txt");
    TF_CHECK_INT(rofs_readdir(d, &entry), 1);  /* 1 表示结束 */
    TF_CHECK_INT(rofs_closedir(d), ROFS_OK);
    tf_end();

    /*
     * 文件写入会先后产生 CREATE 与 INODE_UPDATE 记录，
     * readdir 必须按 Inode 去重，不能把同一次更新列出两遍。
     */
    tf_begin("rofs/readdir-update");
    TF_CHECK_INT(rofs_open(&fs, "/data/c.txt",
                           ROFS_O_CREAT | ROFS_O_RDWR, &f), ROFS_OK);
    TF_CHECK_INT(rofs_write(f, "abc", 3), 3);
    TF_CHECK_INT(rofs_close(f), ROFS_OK);
    TF_CHECK_INT(rofs_opendir(&fs, "/data", &d), ROFS_OK);
    n = 0;
    while (rofs_readdir(d, &entry) == 0) {
        n++;
    }
    TF_CHECK_INT(n, 2);                     /* b.txt + c.txt，各一次 */
    TF_CHECK_INT(rofs_closedir(d), ROFS_OK);
    tf_end();

    /* ---------------- 空间统计 ---------------- */
    tf_begin("rofs/statfs");
    TF_CHECK_INT(rofs_statfs(&fs, &sfs), ROFS_OK);
    TF_CHECK(sfs.total_bytes > 0u);
    TF_CHECK(sfs.used_bytes <= sfs.total_bytes);
    TF_CHECK(sfs.free_bytes <= sfs.total_bytes);
    TF_CHECK_INT(sfs.block_size, T_BLOCK_SIZE);
    tf_end();

    /* ---------------- 卸载后重新挂载，验证持久化 ---------------- */
    tf_begin("rofs/remount");
    TF_CHECK_INT(rofs_unmount(&fs), ROFS_OK);
    TF_CHECK_INT(rofs_mount(&fs), ROFS_OK);
    memset(buf, 0, sizeof(buf));
    n = read_all(&fs, "/data/b.txt", buf, sizeof(buf));
    TF_CHECK_INT(n, 5);
    TF_CHECK_STR(buf, "hello");
    tf_end();

    /* ---------------- 删除 ---------------- */
    tf_begin("rofs/remove");
    TF_CHECK_INT(rofs_rmdir(&fs, "/data"), ROFS_ERR_NOTEMPTY);
    TF_CHECK_INT(rofs_unlink(&fs, "/data/b.txt"), ROFS_OK);
    TF_CHECK_INT(rofs_unlink(&fs, "/data/c.txt"), ROFS_OK);
    TF_CHECK_INT(rofs_unlink(&fs, "/big.bin"), ROFS_OK);
    TF_CHECK_INT(rofs_rmdir(&fs, "/data"), ROFS_OK);
    TF_CHECK_INT(rofs_exists(&fs, "/data"), 0);
    tf_end();
}
