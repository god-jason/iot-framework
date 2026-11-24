/*
 * mfs_test.c - RAM 块设备上的 MFS 单元测试
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
#include "mfs.h"

#include <string.h>

#define T_BLOCK_SIZE   4096u                   /* 模拟块大小 */
#define T_BLOCK_COUNT  16u                     /* 16 块 = 64KB */
#define T_DISK_SIZE    (T_BLOCK_SIZE * T_BLOCK_COUNT)

static uint8_t s_disk[T_DISK_SIZE];            /* 模拟 Flash */
static uint32_t s_erase_count[T_BLOCK_COUNT];  /* 每块实际擦除次数，用于验证磨损均衡 */

/* 擦除故障注入：指定物理块号擦除失败，用于验证坏块处理 */
static uint32_t s_erase_fail_block = 0xFFFFFFFFu;
static uint32_t s_erase_fail_times = 0u;

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

/* 擦除：要求按整块对齐，恢复为 0xFF，并累计每块擦除次数 */
static int test_erase(void *ctx, uint32_t addr, uint32_t size)
{
    uint32_t off;

    (void)ctx;
    if (addr > T_DISK_SIZE || size > T_DISK_SIZE - addr ||
        (addr % T_BLOCK_SIZE) != 0u || (size % T_BLOCK_SIZE) != 0u) {
        return -1;
    }
    /* 故障注入：模拟坏块擦除失败 */
    if (s_erase_fail_times > 0u && addr / T_BLOCK_SIZE == s_erase_fail_block) {
        s_erase_fail_times--;
        return -1;
    }
    memset(&s_disk[addr], 0xFF, size);
    for (off = 0u; off < size; off += T_BLOCK_SIZE) {
        s_erase_count[(addr + off) / T_BLOCK_SIZE]++;
    }
    return 0;
}

/* 同步：内存设备无需处理 */
static int test_sync(void *ctx)
{
    (void)ctx;
    return 0;
}

/* 打开并一次性读完整个文件，返回字节数或负错误码 */
static int read_all(mfs_t *fs, const char *path, void *buf, uint32_t cap)
{
    mfs_file_t *f = NULL;
    int n;
    int err;

    err = mfs_open(fs, path, MFS_O_RDONLY, &f);
    if (err != MFS_OK) {
        return err;
    }
    n = mfs_read(f, buf, cap);
    (void)mfs_close(f);
    return n;
}

/*
 * 从 offset 开始写入 total 字节，每次最多 600B，内容全为 value。
 * 用于快速把数据铺满多个块，触发块分配与 GC。
 */
static int write_fill(mfs_t *fs, const char *path, uint32_t offset,
                      uint32_t total, uint8_t value)
{
    mfs_file_t *f = NULL;
    uint8_t buf[600];
    uint32_t done = 0u;
    int err;

    memset(buf, value, sizeof(buf));
    err = mfs_open(fs, path, MFS_O_CREAT | MFS_O_RDWR, &f);
    if (err != MFS_OK) {
        return err;
    }
    err = mfs_seek(f, (mfs_off_t)offset, MFS_SEEK_SET);
    while (err == MFS_OK && done < total) {
        uint32_t n = total - done;
        int w;

        if (n > (uint32_t)sizeof(buf)) {
            n = (uint32_t)sizeof(buf);
        }
        w = mfs_write(f, buf, n);
        if (w != (int)n) {
            err = (w < 0) ? w : MFS_ERR_IO;
            break;
        }
        done += n;
    }
    (void)mfs_close(f);
    return err;
}

void ut_mfs(void)
{
    static mfs_t fs;                          /* 静态实例，模拟嵌入式用法 */
    static uint8_t workbuf[MFS_WORKBUF_SIZE]; /* 必需工作缓冲区 */
    mfs_blockdev_t dev;
    mfs_config_t cfg;
    mfs_file_t *f = NULL;
    mfs_dir_t *d = NULL;
    mfs_dirent_t entry;
    mfs_stat_t st;
    mfs_statfs_t sfs;
    char buf[32];
    uint8_t big_w[600];
    uint8_t big_r[600];
    uint32_t i;
    uint32_t j;
    uint32_t before_free;
    uint32_t before_used;
    uint32_t min_ec;
    uint32_t max_ec;
    uint32_t avg_ec;
    uint32_t bad;
    uint32_t victim;
    uint32_t ec_before[T_BLOCK_COUNT];
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

    /* MFS 配置：工作缓冲必需，句柄数量取编译期上限 */
    memset(&cfg, 0, sizeof(cfg));
    cfg.workbuf = workbuf;
    cfg.workbuf_size = sizeof(workbuf);
    cfg.max_open_files = MFS_MAX_OPEN_FILES;
    cfg.max_open_dirs = MFS_MAX_OPEN_DIRS;

    memset(s_disk, 0xFF, sizeof(s_disk));
    memset(s_erase_count, 0, sizeof(s_erase_count));

    /* ---------------- 格式化与挂载 ---------------- */
    tf_begin("mfs/format-mount");
    TF_CHECK_INT(mfs_init(&fs, &dev, &cfg), MFS_OK);
    TF_CHECK_INT(mfs_format(&fs), MFS_OK);
    TF_CHECK_INT(mfs_isdir(&fs, "/"), 1);
    TF_CHECK_INT(mfs_mkdir(&fs, "/data"), MFS_OK);
    TF_CHECK_INT(mfs_mkdir(&fs, "/logs"), MFS_OK);
    TF_CHECK_INT(mfs_isdir(&fs, "/data"), 1);
    TF_CHECK_INT(mfs_mkdir(&fs, "/data"), MFS_ERR_EXIST);
    tf_end();

    /* ---------------- 基础写读 ---------------- */
    tf_begin("mfs/file-write-read");
    TF_CHECK_INT(mfs_open(&fs, "/data/a.txt",
                           MFS_O_CREAT | MFS_O_RDWR, &f), MFS_OK);
    TF_CHECK(f != NULL);
    TF_CHECK_INT(mfs_write(f, "hello world", 11), 11);
    TF_CHECK_INT(mfs_tell(f), 11);
    TF_CHECK_INT(mfs_size(f), 11);
    TF_CHECK_INT(mfs_seek(f, 0, MFS_SEEK_SET), MFS_OK);
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(mfs_read(f, buf, 11), 11);
    TF_CHECK_STR(buf, "hello world");
    TF_CHECK_INT(mfs_close(f), MFS_OK);
    tf_end();

    /* ---------------- 只读打开与 SEEK_END ---------------- */
    tf_begin("mfs/file-readonly-seek");
    memset(buf, 0, sizeof(buf));
    n = read_all(&fs, "/data/a.txt", buf, sizeof(buf));
    TF_CHECK_INT(n, 11);
    TF_CHECK_STR(buf, "hello world");

    TF_CHECK_INT(mfs_open(&fs, "/data/a.txt", MFS_O_RDONLY, &f), MFS_OK);
    TF_CHECK_INT(mfs_seek(f, -3, MFS_SEEK_END), MFS_OK);
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(mfs_read(f, buf, 3), 3);
    TF_CHECK_STR(buf, "rld");
    TF_CHECK_INT(mfs_close(f), MFS_OK);
    tf_end();

    /* ---------------- 追加、随机写、截断 ---------------- */
    tf_begin("mfs/append-random-truncate");
    TF_CHECK_INT(mfs_open(&fs, "/data/a.txt",
                           MFS_O_WRONLY | MFS_O_APPEND, &f), MFS_OK);
    TF_CHECK_INT(mfs_write(f, "!", 1), 1);
    TF_CHECK_INT(mfs_close(f), MFS_OK);

    /* 在偏移 6 覆盖 "MFS1"，其余字节保持不变 */
    TF_CHECK_INT(mfs_open(&fs, "/data/a.txt", MFS_O_RDWR, &f), MFS_OK);
    TF_CHECK_INT(mfs_seek(f, 6, MFS_SEEK_SET), MFS_OK);
    TF_CHECK_INT(mfs_write(f, "MFS1", 4), 4);
    TF_CHECK_INT(mfs_close(f), MFS_OK);

    memset(buf, 0, sizeof(buf));
    n = read_all(&fs, "/data/a.txt", buf, sizeof(buf));
    TF_CHECK_INT(n, 12);
    TF_CHECK_STR(buf, "hello MFS1d!");

    /* 截断到 5，只保留 "hello" */
    TF_CHECK_INT(mfs_open(&fs, "/data/a.txt", MFS_O_WRONLY, &f), MFS_OK);
    TF_CHECK_INT(mfs_truncate(f, 5), MFS_OK);
    TF_CHECK_INT(mfs_close(f), MFS_OK);

    memset(buf, 0, sizeof(buf));
    n = read_all(&fs, "/data/a.txt", buf, sizeof(buf));
    TF_CHECK_INT(n, 5);
    TF_CHECK_STR(buf, "hello");
    tf_end();

    /* ---------------- 跨 Chunk 文件（600B = 3 个 Chunk） ---------------- */
    tf_begin("mfs/multi-chunk");
    for (i = 0; i < sizeof(big_w); i++) {
        big_w[i] = (uint8_t)(i * 7u + 3u);
    }
    TF_CHECK_INT(mfs_open(&fs, "/big.bin",
                           MFS_O_CREAT | MFS_O_RDWR, &f), MFS_OK);
    TF_CHECK_INT(mfs_write(f, big_w, sizeof(big_w)), (int)sizeof(big_w));
    TF_CHECK_INT(mfs_size(f), (int)sizeof(big_w));
    TF_CHECK_INT(mfs_close(f), MFS_OK);

    memset(big_r, 0, sizeof(big_r));
    n = read_all(&fs, "/big.bin", big_r, sizeof(big_r));
    TF_CHECK_INT(n, (int)sizeof(big_r));
    TF_CHECK_MEM(big_r, big_w, sizeof(big_w));
    tf_end();

    /* ---------------- 截断后扩展：100..299 必须是空洞 0 ---------------- */
    tf_begin("mfs/truncate-hole");
    TF_CHECK_INT(mfs_open(&fs, "/big.bin", MFS_O_RDWR, &f), MFS_OK);
    TF_CHECK_INT(mfs_truncate(f, 100), MFS_OK);
    TF_CHECK_INT(mfs_close(f), MFS_OK);
    TF_CHECK_INT(mfs_open(&fs, "/big.bin", MFS_O_RDWR, &f), MFS_OK);
    TF_CHECK_INT(mfs_seek(f, 300, MFS_SEEK_SET), MFS_OK);
    TF_CHECK_INT(mfs_write(f, "X", 1), 1);
    TF_CHECK_INT(mfs_close(f), MFS_OK);

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
    tf_begin("mfs/rename-stat");
    TF_CHECK_INT(mfs_rename(&fs, "/data/a.txt", "/data/b.txt"), MFS_OK);
    TF_CHECK_INT(mfs_exists(&fs, "/data/a.txt"), 0);
    TF_CHECK_INT(mfs_exists(&fs, "/data/b.txt"), 1);
    TF_CHECK_INT(mfs_stat(&fs, "/data/b.txt", &st), MFS_OK);
    TF_CHECK_STR(st.name, "b.txt");
    TF_CHECK_INT(st.size, 5);
    TF_CHECK_INT(st.type, 1);
    tf_end();

    /* ---------------- 目录遍历 ---------------- */
    tf_begin("mfs/readdir");
    TF_CHECK_INT(mfs_opendir(&fs, "/data", &d), MFS_OK);
    TF_CHECK(d != NULL);
    memset(&entry, 0, sizeof(entry));
    TF_CHECK_INT(mfs_readdir(d, &entry), 0);
    TF_CHECK_STR(entry.name, "b.txt");
    TF_CHECK_INT(mfs_readdir(d, &entry), 1);  /* 1 表示结束 */
    TF_CHECK_INT(mfs_closedir(d), MFS_OK);
    tf_end();

    /*
     * 文件写入会先后产生 CREATE 与 INODE_UPDATE 记录，
     * readdir 必须按 Inode 去重，不能把同一次更新列出两遍。
     */
    tf_begin("mfs/readdir-update");
    TF_CHECK_INT(mfs_open(&fs, "/data/c.txt",
                           MFS_O_CREAT | MFS_O_RDWR, &f), MFS_OK);
    TF_CHECK_INT(mfs_write(f, "abc", 3), 3);
    TF_CHECK_INT(mfs_close(f), MFS_OK);
    TF_CHECK_INT(mfs_opendir(&fs, "/data", &d), MFS_OK);
    n = 0;
    while (mfs_readdir(d, &entry) == 0) {
        n++;
    }
    TF_CHECK_INT(n, 2);                     /* b.txt + c.txt，各一次 */
    TF_CHECK_INT(mfs_closedir(d), MFS_OK);
    tf_end();

    /* ---------------- 空间统计 ---------------- */
    tf_begin("mfs/statfs");
    TF_CHECK_INT(mfs_statfs(&fs, &sfs), MFS_OK);
    TF_CHECK(sfs.total_bytes > 0u);
    TF_CHECK(sfs.used_bytes <= sfs.total_bytes);
    TF_CHECK(sfs.free_bytes <= sfs.total_bytes);
    TF_CHECK_INT(sfs.block_size, T_BLOCK_SIZE);
    tf_end();

    /* ---------------- 卸载后重新挂载，验证持久化 ---------------- */
    tf_begin("mfs/remount");
    TF_CHECK_INT(mfs_unmount(&fs), MFS_OK);
    TF_CHECK_INT(mfs_mount(&fs), MFS_OK);
    memset(buf, 0, sizeof(buf));
    n = read_all(&fs, "/data/b.txt", buf, sizeof(buf));
    TF_CHECK_INT(n, 5);
    TF_CHECK_STR(buf, "hello");
    tf_end();

    /* ---------------- 删除 ---------------- */
    tf_begin("mfs/remove");
    TF_CHECK_INT(mfs_rmdir(&fs, "/data"), MFS_ERR_NOTEMPTY);
    TF_CHECK_INT(mfs_unlink(&fs, "/data/b.txt"), MFS_OK);
    TF_CHECK_INT(mfs_unlink(&fs, "/data/c.txt"), MFS_OK);
    TF_CHECK_INT(mfs_unlink(&fs, "/big.bin"), MFS_OK);
    TF_CHECK_INT(mfs_rmdir(&fs, "/data"), MFS_OK);
    TF_CHECK_INT(mfs_exists(&fs, "/data"), 0);
    tf_end();

    /*
     * ---------------- GC：回收失效记录 ----------------
     * 反复覆盖同一文件，旧 DATA 记录全部失效；
     * GC 复制少量有效记录后擦除牺牲块，数据必须保持不变。
     */
    tf_begin("mfs/gc-reclaim");
    TF_CHECK_INT(mfs_format(&fs), MFS_OK);
    TF_CHECK_INT(mfs_mkdir(&fs, "/gc"), MFS_OK);
    TF_CHECK_INT(mfs_open(&fs, "/gc/f.bin",
                           MFS_O_CREAT | MFS_O_RDWR, &f), MFS_OK);
    for (n = 0; n < 6; n++) {
        memset(big_w, 0x10 + n, sizeof(big_w));   /* 每次内容不同 */
        TF_CHECK_INT(mfs_seek(f, 0, MFS_SEEK_SET), MFS_OK);
        TF_CHECK_INT(mfs_write(f, big_w, sizeof(big_w)),
                     (int)sizeof(big_w));
    }
    TF_CHECK_INT(mfs_close(f), MFS_OK);

    TF_CHECK_INT(mfs_statfs(&fs, &sfs), MFS_OK);
    before_free = sfs.free_blocks;
    before_used = sfs.used_bytes;

    TF_CHECK_INT(mfs_gc(&fs), MFS_OK);

    TF_CHECK_INT(mfs_statfs(&fs, &sfs), MFS_OK);
    TF_CHECK(sfs.free_blocks > before_free);      /* 至少回收一个块 */
    TF_CHECK(sfs.used_bytes < before_used);
    TF_CHECK_INT(mfs_check(&fs), MFS_OK);       /* 全盘 CRC 仍正确 */

    memset(big_r, 0, sizeof(big_r));
    n = read_all(&fs, "/gc/f.bin", big_r, sizeof(big_r));
    TF_CHECK_INT(n, (int)sizeof(big_r));
    TF_CHECK_MEM(big_r, big_w, sizeof(big_w));

    /* 重新挂载后 GC 结果仍然有效 */
    TF_CHECK_INT(mfs_unmount(&fs), MFS_OK);
    TF_CHECK_INT(mfs_mount(&fs), MFS_OK);
    memset(big_r, 0, sizeof(big_r));
    n = read_all(&fs, "/gc/f.bin", big_r, sizeof(big_r));
    TF_CHECK_INT(n, (int)sizeof(big_r));
    TF_CHECK_MEM(big_r, big_w, sizeof(big_w));
    tf_end();

    /*
     * ---------------- 磨损均衡 ----------------
     * GC 擦除的牺牲块擦除次数 +1，成为高磨损块；
     * 后续分配应优先选择低擦除次数的空闲块，避免反复磨损同一块。
     */
    tf_begin("mfs/wear-level");
    TF_CHECK_INT(mfs_format(&fs), MFS_OK);
    TF_CHECK_INT(mfs_mkdir(&fs, "/wear"), MFS_OK);
    TF_CHECK_INT(write_fill(&fs, "/wear/w.bin", 0u, 5000u, 0x11), MFS_OK);
    TF_CHECK_INT(write_fill(&fs, "/wear/w.bin", 0u, 5000u, 0x22), MFS_OK);

    /* 记录 GC 前各块擦除次数，GC 后对比找出牺牲块 */
    memcpy(ec_before, s_erase_count, sizeof(ec_before));
    TF_CHECK_INT(mfs_gc(&fs), MFS_OK);
    victim = 0xFFFFFFFFu;
    for (j = 2u; j < T_BLOCK_COUNT; j++) {
        if (s_erase_count[j] > ec_before[j]) {
            victim = j - 2u;              /* 物理块 0/1 为 Superblock */
        }
    }
    TF_CHECK(victim != 0xFFFFFFFFu);

    TF_CHECK_INT(mfs_erase_stat(&fs, &min_ec, &max_ec, &avg_ec), MFS_OK);
    TF_CHECK(max_ec >= s_erase_count[victim + 2u]);
    TF_CHECK(max_ec > min_ec);            /* 擦除次数已分散 */

    /* 大量写入触发多次块分配，高磨损块不应被选中 */
    TF_CHECK_INT(write_fill(&fs, "/wear/fill.bin", 0u, 16000u, 0x33), MFS_OK);
    TF_CHECK(fs.active_block != victim);

    /* 擦除次数持久化在块头中，重新挂载后仍可读出 */
    TF_CHECK_INT(mfs_unmount(&fs), MFS_OK);
    TF_CHECK_INT(mfs_mount(&fs), MFS_OK);
    TF_CHECK_INT(mfs_erase_stat(&fs, &min_ec, &max_ec, &avg_ec), MFS_OK);
    TF_CHECK(max_ec > min_ec);
    tf_end();

    /*
     * ---------------- GC：复制有效记录 ----------------
     * 牺牲块内混合有效与失效记录：
     *   - 有效 DATA 含 256B 整 Chunk 与 88B 部分 Chunk；
     *   - GC 只复制有效记录，复制后两个文件内容必须完全一致。
     */
    tf_begin("mfs/gc-live-copy");
    TF_CHECK_INT(mfs_format(&fs), MFS_OK);
    TF_CHECK_INT(mfs_mkdir(&fs, "/gc2"), MFS_OK);

    memset(big_w, 0xA5, sizeof(big_w));
    TF_CHECK_INT(mfs_open(&fs, "/gc2/live.bin",
                           MFS_O_CREAT | MFS_O_RDWR, &f), MFS_OK);
    TF_CHECK_INT(mfs_write(f, big_w, sizeof(big_w)), (int)sizeof(big_w));
    /* 只覆盖最后一个 Chunk，前两个 Chunk 仍是最新有效记录 */
    memset(big_w + 512, 0x5A, sizeof(big_w) - 512);
    TF_CHECK_INT(mfs_seek(f, 512, MFS_SEEK_SET), MFS_OK);
    TF_CHECK_INT(mfs_write(f, big_w + 512, sizeof(big_w) - 512),
                 (int)(sizeof(big_w) - 512));
    TF_CHECK_INT(mfs_close(f), MFS_OK);

    /* 反复覆盖另一个文件，把活动块推走，使块 0 剩下混合记录 */
    TF_CHECK_INT(write_fill(&fs, "/gc2/junk.bin", 0u, 1200u, 0x77), MFS_OK);
    TF_CHECK_INT(write_fill(&fs, "/gc2/junk.bin", 0u, 1200u, 0x77), MFS_OK);
    TF_CHECK_INT(write_fill(&fs, "/gc2/junk.bin", 0u, 1200u, 0x77), MFS_OK);

    TF_CHECK_INT(mfs_statfs(&fs, &sfs), MFS_OK);
    before_free = sfs.free_blocks;
    before_used = sfs.used_bytes;
    TF_CHECK_INT(mfs_gc(&fs), MFS_OK);
    TF_CHECK_INT(mfs_statfs(&fs, &sfs), MFS_OK);
    TF_CHECK(sfs.free_blocks > before_free);
    TF_CHECK(sfs.used_bytes < before_used);
    TF_CHECK_INT(mfs_check(&fs), MFS_OK);

    memset(big_r, 0, sizeof(big_r));
    n = read_all(&fs, "/gc2/live.bin", big_r, sizeof(big_r));
    TF_CHECK_INT(n, (int)sizeof(big_r));
    TF_CHECK_MEM(big_r, big_w, sizeof(big_w));

    memset(big_r, 0, sizeof(big_r));
    n = read_all(&fs, "/gc2/junk.bin", big_r, sizeof(big_r));
    TF_CHECK_INT(n, (int)sizeof(big_r));
    TF_CHECK_INT(big_r[0], 0x77);
    TF_CHECK_INT(big_r[sizeof(big_r) - 1], 0x77);
    TF_CHECK_INT(mfs_stat(&fs, "/gc2/junk.bin", &st), MFS_OK);
    TF_CHECK_INT(st.size, 1200);

    /* 重挂载后复制出的记录仍然有效 */
    TF_CHECK_INT(mfs_unmount(&fs), MFS_OK);
    TF_CHECK_INT(mfs_mount(&fs), MFS_OK);
    memset(big_r, 0, sizeof(big_r));
    n = read_all(&fs, "/gc2/live.bin", big_r, sizeof(big_r));
    TF_CHECK_INT(n, (int)sizeof(big_r));
    TF_CHECK_MEM(big_r, big_w, sizeof(big_w));
    tf_end();

    /*
     * ---------------- GC：有效比例阈值 ----------------
     * 块内有效数据 >= 75% 时默认不回收（搬移不划算），
     * 此时 mfs_gc 应返回成功但不改变空间统计。
     */
    tf_begin("mfs/gc-threshold");
    TF_CHECK_INT(mfs_format(&fs), MFS_OK);
    /* 按 256B 对齐整块写，保证块内记录全部有效（无覆盖产生的垃圾） */
    memset(big_w, 0x66, sizeof(big_w));
    TF_CHECK_INT(mfs_open(&fs, "/hot.bin",
                           MFS_O_CREAT | MFS_O_RDWR, &f), MFS_OK);
    for (i = 0u; i < 14u; i++) {
        TF_CHECK_INT(mfs_write(f, big_w, MFS_CHUNK_SIZE),
                     MFS_CHUNK_SIZE);
    }
    TF_CHECK_INT(mfs_write(f, big_w, 16u), 16);
    TF_CHECK_INT(mfs_close(f), MFS_OK);
    TF_CHECK_INT(mfs_statfs(&fs, &sfs), MFS_OK);
    before_free = sfs.free_blocks;
    before_used = sfs.used_bytes;
    TF_CHECK_INT(mfs_gc(&fs), MFS_OK);
    TF_CHECK_INT(mfs_statfs(&fs, &sfs), MFS_OK);
    TF_CHECK_INT(sfs.free_blocks, (int)before_free);
    TF_CHECK_INT(sfs.used_bytes, (int)before_used);
    TF_CHECK_INT(mfs_gc_needed(&fs), 0);       /* 空闲充足，无需 GC */

    /* 数据未搬移也保持可读 */
    memset(big_r, 0, sizeof(big_r));
    n = read_all(&fs, "/hot.bin", big_r, sizeof(big_r));
    TF_CHECK_INT(n, (int)sizeof(big_r));
    TF_CHECK_INT(big_r[0], 0x66);
    TF_CHECK_INT(big_r[sizeof(big_r) - 1], 0x66);
    tf_end();

    /*
     * ---------------- GC：目录句柄保护 ----------------
     * readdir 迭代期间搬移记录会让遍历看到重复项，
     * 因此有目录句柄打开时 GC 必须返回 BUSY。
     */
    tf_begin("mfs/gc-busy");
    TF_CHECK_INT(mfs_format(&fs), MFS_OK);
    TF_CHECK_INT(mfs_opendir(&fs, "/", &d), MFS_OK);
    TF_CHECK(d != NULL);
    TF_CHECK_INT(mfs_gc(&fs), MFS_ERR_BUSY);
    TF_CHECK_INT(mfs_closedir(d), MFS_OK);
    TF_CHECK_INT(mfs_gc(&fs), MFS_OK);
    tf_end();

    /*
     * ---------------- 坏块管理 ----------------
     * 标记坏块后：不计入可用容量、重挂载/格式化后标记保留、分配时跳过。
     */
    tf_begin("mfs/bad-block");
    TF_CHECK_INT(mfs_format(&fs), MFS_OK);
    TF_CHECK_INT(mfs_statfs(&fs, &sfs), MFS_OK);
    TF_CHECK_INT(sfs.bad_blocks, 0);
    TF_CHECK_INT(sfs.free_blocks, (int)(T_BLOCK_COUNT - 2u - 1u));

    bad = (fs.active_block + 1u) % fs.block_count;
    TF_CHECK_INT(mfs_mark_bad(&fs, fs.active_block), MFS_ERR_INVAL);
    TF_CHECK_INT(mfs_mark_bad(&fs, fs.block_count), MFS_ERR_INVAL);
    TF_CHECK_INT(mfs_mark_bad(&fs, 0xFFFFFFFFu), MFS_ERR_INVAL);
    TF_CHECK_INT(mfs_mark_bad(&fs, bad), MFS_OK);
    TF_CHECK_INT(mfs_mark_bad(&fs, bad), MFS_ERR_INVAL); /* 不可重复标记 */

    TF_CHECK_INT(mfs_statfs(&fs, &sfs), MFS_OK);
    TF_CHECK_INT(sfs.bad_blocks, 1);
    TF_CHECK_INT(sfs.free_blocks, (int)(T_BLOCK_COUNT - 2u - 2u));

    /* 标记坏块后仍可正常写入读取 */
    TF_CHECK_INT(mfs_open(&fs, "/f.bin",
                           MFS_O_CREAT | MFS_O_RDWR, &f), MFS_OK);
    TF_CHECK_INT(mfs_write(f, "bad block ok", 12), 12);
    TF_CHECK_INT(mfs_close(f), MFS_OK);
    memset(buf, 0, sizeof(buf));
    n = read_all(&fs, "/f.bin", buf, sizeof(buf));
    TF_CHECK_INT(n, 12);
    TF_CHECK_STR(buf, "bad block ok");

    /* 重新挂载：坏块计数与块头都保留 */
    TF_CHECK_INT(mfs_unmount(&fs), MFS_OK);
    TF_CHECK_INT(mfs_mount(&fs), MFS_OK);
    TF_CHECK_INT(mfs_statfs(&fs, &sfs), MFS_OK);
    TF_CHECK_INT(sfs.bad_blocks, 1);
    TF_CHECK_INT(sfs.free_blocks, (int)(T_BLOCK_COUNT - 2u - 2u));

    /* 格式化不会复活坏块 */
    TF_CHECK_INT(mfs_format(&fs), MFS_OK);
    TF_CHECK_INT(mfs_statfs(&fs, &sfs), MFS_OK);
    TF_CHECK_INT(sfs.bad_blocks, 1);
    TF_CHECK_INT(sfs.free_blocks, (int)(T_BLOCK_COUNT - 2u - 2u));

    /* 新数据分配时跳过坏块 */
    TF_CHECK_INT(write_fill(&fs, "/fill.bin", 0u, 16000u, 0x44), MFS_OK);
    TF_CHECK(fs.active_block != bad);
    tf_end();

    /*
     * ---------------- 坏块：GC 跳过 ----------------
     * 制造可回收的垃圾块后执行 GC：
     *   - 坏块不能被选中、不能被擦除（擦除次数不增加）；
     *   - 其他垃圾块正常回收，数据与坏块标记保持不变。
     */
    tf_begin("mfs/bad-skip-gc");
    memset(s_disk, 0xFF, sizeof(s_disk));
    memset(s_erase_count, 0, sizeof(s_erase_count));
    TF_CHECK_INT(mfs_format(&fs), MFS_OK);

    bad = (fs.active_block + 1u) % fs.block_count;
    TF_CHECK_INT(mfs_mark_bad(&fs, bad), MFS_OK);

    TF_CHECK_INT(write_fill(&fs, "/junk.bin", 0u, 1200u, 0x21), MFS_OK);
    TF_CHECK_INT(write_fill(&fs, "/junk.bin", 0u, 1200u, 0x22), MFS_OK);
    TF_CHECK_INT(write_fill(&fs, "/junk.bin", 0u, 1200u, 0x22), MFS_OK);
    TF_CHECK_INT(write_fill(&fs, "/junk.bin", 0u, 1200u, 0x22), MFS_OK);

    TF_CHECK_INT(mfs_statfs(&fs, &sfs), MFS_OK);
    before_free = sfs.free_blocks;
    memcpy(ec_before, s_erase_count, sizeof(ec_before));
    TF_CHECK_INT(mfs_gc(&fs), MFS_OK);

    /* 坏块没有被擦除，其他垃圾块被回收 */
    TF_CHECK_INT(s_erase_count[bad + 2u], (int)ec_before[bad + 2u]);
    TF_CHECK_INT(mfs_statfs(&fs, &sfs), MFS_OK);
    TF_CHECK(sfs.free_blocks > before_free);
    TF_CHECK_INT(sfs.bad_blocks, 1);

    memset(big_r, 0, sizeof(big_r));
    n = read_all(&fs, "/junk.bin", big_r, sizeof(big_r));
    TF_CHECK_INT(n, (int)sizeof(big_r));
    TF_CHECK_INT(big_r[0], 0x22);
    TF_CHECK_INT(big_r[sizeof(big_r) - 1], 0x22);

    TF_CHECK_INT(mfs_unmount(&fs), MFS_OK);
    TF_CHECK_INT(mfs_mount(&fs), MFS_OK);
    TF_CHECK_INT(mfs_statfs(&fs, &sfs), MFS_OK);
    TF_CHECK_INT(sfs.bad_blocks, 1);
    tf_end();

    /*
     * ---------------- 坏块：擦除失败自动标记 ----------------
     * 格式化时某块擦除失败：
     *   - 写 BAD 头后继续格式化；
     *   - 坏块不计入可用容量，重挂载后标记保留；
     *   - 之后仍可正常读写，且分配时跳过该块。
     */
    tf_begin("mfs/bad-erase-fail");
    memset(s_disk, 0xFF, sizeof(s_disk));
    memset(s_erase_count, 0, sizeof(s_erase_count));
    TF_CHECK_INT(mfs_format(&fs), MFS_OK);

    bad = 5u;                                  /* 池内块号 */
    /* 模拟该块已擦除，使失败的擦除后 BAD 头仍可写入 */
    memset(&s_disk[(bad + 2u) * T_BLOCK_SIZE], 0xFF, T_BLOCK_SIZE);
    s_erase_fail_block = bad + 2u;
    s_erase_fail_times = 1u;
    TF_CHECK_INT(mfs_format(&fs), MFS_OK);   /* 失败后应转标坏块并继续 */
    s_erase_fail_block = 0xFFFFFFFFu;

    TF_CHECK_INT(mfs_statfs(&fs, &sfs), MFS_OK);
    TF_CHECK_INT(sfs.bad_blocks, 1);
    TF_CHECK_INT(sfs.free_blocks, (int)(T_BLOCK_COUNT - 2u - 1u - 1u));

    /* 坏块标记持久化 */
    TF_CHECK_INT(mfs_unmount(&fs), MFS_OK);
    TF_CHECK_INT(mfs_mount(&fs), MFS_OK);
    TF_CHECK_INT(mfs_statfs(&fs, &sfs), MFS_OK);
    TF_CHECK_INT(sfs.bad_blocks, 1);
    TF_CHECK_INT(sfs.free_blocks, (int)(T_BLOCK_COUNT - 2u - 1u - 1u));

    /* 文件读写仍然正常 */
    TF_CHECK_INT(mfs_open(&fs, "/ok.bin",
                           MFS_O_CREAT | MFS_O_RDWR, &f), MFS_OK);
    TF_CHECK_INT(mfs_write(f, "after bad erase", 15), 15);
    TF_CHECK_INT(mfs_close(f), MFS_OK);
    memset(buf, 0, sizeof(buf));
    n = read_all(&fs, "/ok.bin", buf, sizeof(buf));
    TF_CHECK_INT(n, 15);
    TF_CHECK_STR(buf, "after bad erase");

    /* 后续分配跳过坏块 */
    TF_CHECK_INT(write_fill(&fs, "/fill2.bin", 0u, 16000u, 0x55), MFS_OK);
    TF_CHECK(fs.active_block != bad);
    tf_end();
}
