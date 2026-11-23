/*
 * rofs_file.c - 文件打开、读写、Seek 与截断
 * ============================================================================
 *
 * DATA 记录 Payload 格式（小端）：
 *
 *   +0   inode       4B   文件 Inode
 *   +4   chunk       4B   逻辑 Chunk 号
 *   +8   generation  4B   写入代号（取写入时的全局 seq）
 *   +12  epoch       4B   数据代次（保留，用于整文件失效）
 *   +16  data_size   2B   本 Chunk 有效字节数（<= 256）
 *   +18  flags       2B   保留
 *   +20  reserved    4B   对齐填充
 *   +24  data        data_size 字节
 *
 * DATA_DELETE 记录 Payload 格式：
 *
 *   +0   inode       4B
 *   +4   start_chunk 4B   起始 Chunk（含）
 *   +8   count       4B   失效 Chunk 数量
 *   +12  epoch       4B   仅失效同 epoch 的数据
 *
 * 读取规则：
 *   - 同一 (inode, chunk, epoch) 取 generation 最大的 DATA 记录；
 *   - 若存在 sequence 更大的 DATA_DELETE 覆盖该 Chunk，则旧数据失效；
 *   - 没有有效数据时按“空洞”处理，读接口补 0。
 *
 * 写入策略：
 *   - Chunk 对齐的整块写：直接追加 DATA 记录；
 *   - 非对齐/部分写：先读出旧 Chunk 到 workbuf，合并后再整块写；
 *   - 所有 Chunk 写完后，若文件变大，再追加一条 INODE_UPDATE 更新 size。
 *     若中途掉电，size 仍是旧值，超出部分的数据记录不会生效。
 *
 * 截断策略（仅支持缩小）：
 *   - 边界 Chunk 改写为“缩短后的 DATA 记录”，避免后续扩展暴露旧字节；
 *   - 后续完整 Chunk 写一条 DATA_DELETE 批量失效；
 *   - 最后更新 Inode.size。
 */

#include "rofs_internal.h"

/* 单次 Chunk 查询的上下文 */
typedef struct {
    uint32_t inode;    /* 目标文件 */
    uint32_t chunk;    /* 目标 Chunk */
    uint32_t epoch;    /* 目标数据代次 */
    uint8_t found;     /* 是否找到有效 DATA */
    uint32_t gen;      /* 当前最大 generation */
    uint32_t seq;      /* 最大 generation 对应的记录 sequence */
    uint32_t del_seq;  /* 覆盖该 Chunk 的最大 DATA_DELETE sequence */
    uint32_t addr;     /* 最优 DATA 记录地址 */
    uint16_t size;     /* 该 Chunk 有效字节数 */
} data_loc_t;

/*
 * Chunk 查找回调。
 * 同时统计两类记录：
 *   - DATA        ：找出 generation 最大的有效数据；
 *   - DATA_DELETE ：记录覆盖该 Chunk 的最新失效序号。
 * 最终有效性 = DATA.seq > DATA_DELETE.seq。
 */
static int data_find_cb(rofs_t *fs, const rofs_rec_hdr_t *hdr,
                        uint32_t rec_addr, void *ctx)
{
    data_loc_t *loc = (data_loc_t *)ctx;
    uint8_t prefix[ROFS_DATA_PREFIX];
    int err;

    /* 只关心同一 Inode 的记录 */
    if (hdr->object_id != loc->inode) {
        return ROFS_OK;
    }

    /* --- DATA_DELETE：范围失效 --- */
    if (hdr->type == ROFS_REC_DATA_DELETE) {
        if (hdr->payload_size < 16u) {
            return ROFS_OK;
        }
        err = rofs__rec_read_raw(fs, rec_addr, hdr, 0u, prefix, 16u);
        if (err != ROFS_OK) {
            return err;
        }
        if (rofs__get_u32(&prefix[12]) != loc->epoch) {
            return ROFS_OK;              /* 不同 epoch 的删除不影响当前数据 */
        }
        {
            uint32_t start = rofs__get_u32(&prefix[4]);
            uint32_t count = rofs__get_u32(&prefix[8]);
            /* chunk 落在 [start, start+count) 内则记录失效序号 */
            if (loc->chunk >= start && loc->chunk - start < count &&
                hdr->sequence > loc->del_seq) {
                loc->del_seq = hdr->sequence;
            }
        }
        return ROFS_OK;
    }

    /* --- DATA：找最大 generation --- */
    if (hdr->type != ROFS_REC_DATA) {
        return ROFS_OK;
    }
    if (hdr->payload_size < ROFS_DATA_PREFIX) {
        return ROFS_OK;
    }
    err = rofs__rec_read_raw(fs, rec_addr, hdr, 0u, prefix, sizeof(prefix));
    if (err != ROFS_OK) {
        return err;
    }
    if (rofs__get_u32(&prefix[4]) != loc->chunk) {
        return ROFS_OK;
    }
    if (rofs__get_u32(&prefix[12]) != loc->epoch) {
        return ROFS_OK;
    }
    if (rofs__get_u16(&prefix[16]) > ROFS_CHUNK_SIZE) {
        return ROFS_OK;
    }

    {
        uint32_t gen = rofs__get_u32(&prefix[8]);
        /* generation 大者优先；相同 generation 取后写入者 */
        if (!loc->found || gen > loc->gen ||
            (gen == loc->gen && hdr->sequence > loc->seq)) {
            loc->found = 1u;
            loc->gen = gen;
            loc->seq = hdr->sequence;
            loc->addr = rec_addr;
            loc->size = rofs__get_u16(&prefix[16]);
        }
    }
    return ROFS_OK;
}

/*
 * 读取一个 Chunk 的有效数据到 buf。
 * 返回：
 *   正数：实际字节数；
 *   0   ：空洞（没有有效数据，调用者补 0）；
 *   负数：错误码。
 */
static int data_read(rofs_t *fs, uint32_t inode, uint32_t chunk,
                     uint32_t epoch, uint8_t *buf, uint32_t cap)
{
    data_loc_t loc;
    rofs_rec_hdr_t hdr;
    uint32_t next;
    uint32_t size;
    int err;

    memset(&loc, 0, sizeof(loc));
    loc.inode = inode;
    loc.chunk = chunk;
    loc.epoch = epoch;

    err = rofs__scan_all(fs, data_find_cb, &loc);
    if (err != ROFS_OK) {
        return err;
    }
    if (!loc.found || loc.size == 0u || loc.seq < loc.del_seq) {
        return 0;                        /* 无数据或被截断失效 */
    }

    /* 扫描阶段已校验过 CRC，这里再确认一次记录完整性 */
    err = rofs__rec_read(fs, loc.addr, &hdr, NULL, 0u, &next);
    if (err != ROFS_OK) {
        return err;
    }

    size = loc.size;
    if (size > cap) {
        size = cap;
    }
    /* 只读取 Payload 的数据部分，跳过 24B 前缀 */
    err = rofs__rec_read_raw(fs, loc.addr, &hdr, ROFS_DATA_PREFIX, buf, size);
    if (err != ROFS_OK) {
        return err;
    }
    return (int)size;
}

/*
 * 写入一个 Chunk（最多 256B）。
 * generation 使用当前 fs->seq，保证后写入的记录一定更新。
 */
static int data_write(rofs_t *fs, uint32_t inode, uint32_t chunk,
                      uint32_t epoch, const void *data, uint32_t size)
{
    uint8_t prefix[ROFS_DATA_PREFIX];

    if (size > ROFS_CHUNK_SIZE) {
        return ROFS_ERR_PARAM;
    }

    rofs__put_u32(&prefix[0], inode);
    rofs__put_u32(&prefix[4], chunk);
    rofs__put_u32(&prefix[8], fs->seq);      /* generation */
    rofs__put_u32(&prefix[12], epoch);
    rofs__put_u16(&prefix[16], (uint16_t)size);
    rofs__put_u16(&prefix[18], 0u);
    rofs__put_u32(&prefix[20], 0u);

    return rofs__rec_write(fs, ROFS_REC_DATA, inode,
                           prefix, sizeof(prefix), data, size);
}

/*
 * 写一条 DATA_DELETE，失效 [start_chunk, start_chunk + count) 范围。
 * 一条记录可覆盖大量 Chunk，避免截断大文件时产生海量小记录。
 */
static int data_delete(rofs_t *fs, uint32_t inode, uint32_t epoch,
                       uint32_t start_chunk, uint32_t count)
{
    uint8_t prefix[16];

    if (count == 0u) {
        return ROFS_OK;
    }
    rofs__put_u32(&prefix[0], inode);
    rofs__put_u32(&prefix[4], start_chunk);
    rofs__put_u32(&prefix[8], count);
    rofs__put_u32(&prefix[12], epoch);
    return rofs__rec_write(fs, ROFS_REC_DATA_DELETE, inode,
                           prefix, sizeof(prefix), NULL, 0u);
}

/*
 * 缩小 Inode 大小，并使超出范围的数据 Chunk 失效。
 *
 * 两个动作：
 *   1. 若 new_size 落在某个 Chunk 中间，把该 Chunk 改写为只包含前
 *      valid 字节的新 DATA 记录，防止之后扩展文件时读到旧字节；
 *   2. 对后续完整 Chunk 写一条 DATA_DELETE。
 *
 * 最后更新 Inode.size。
 */
static int inode_truncate(rofs_t *fs, rofs_ii_t *info, uint32_t new_size)
{
    uint32_t base;
    uint32_t valid;
    uint32_t start;
    uint32_t end;
    int got;
    int err;

    if (new_size > info->size) {
        return ROFS_ERR_NOTSUP;          /* V1 不支持通过 truncate 扩展 */
    }
    if (new_size == info->size) {
        return ROFS_OK;
    }

    base = new_size / ROFS_CHUNK_SIZE;   /* 边界 Chunk 号 */
    valid = new_size % ROFS_CHUNK_SIZE;  /* 边界 Chunk 内有效字节数 */

    /* 边界 Chunk 改写为缩短后的记录 */
    if (valid != 0u) {
        memset(fs->workbuf, 0, ROFS_CHUNK_SIZE);
        got = data_read(fs, info->inode, base, info->epoch,
                        fs->workbuf, ROFS_CHUNK_SIZE);
        if (got < 0) {
            return got;
        }
        err = data_write(fs, info->inode, base, info->epoch,
                         fs->workbuf, valid);
        if (err != ROFS_OK) {
            return err;
        }
    }

    /* 边界之后的所有完整 Chunk 一次性失效 */
    start = (valid != 0u) ? base + 1u : base;
    end = (info->size + ROFS_CHUNK_SIZE - 1u) / ROFS_CHUNK_SIZE;
    if (end > start) {
        err = data_delete(fs, info->inode, info->epoch, start, end - start);
        if (err != ROFS_OK) {
            return err;
        }
    }

    info->size = new_size;
    return rofs__inode_update(fs, info);
}

/* 从内部句柄池分配一个空闲文件句柄 */
static rofs_file_t *file_alloc(rofs_t *fs)
{
    uint32_t i;

    for (i = 0; i < fs->cfg.max_open_files; i++) {
        if (fs->files[i].used == 0u) {
            return &fs->files[i];
        }
    }
    return NULL;
}

/*
 * 打开文件。
 *
 * 处理顺序：
 *   1. 校验访问模式（低 2 位）；
 *   2. 解析父目录与文件名；
 *   3. 已存在：处理 EXCL/TRUNC/目录等语义；
 *   4. 不存在：按 CREAT 创建空文件；
 *   5. 分配句柄并记录 inode/size/epoch。
 *
 * APPEND 打开时初始位置直接放到文件尾。
 */
int rofs_open(rofs_t *fs, const char *path, uint32_t flags, rofs_file_t **file)
{
    rofs_ii_t parent;
    rofs_ii_t info;
    rofs_file_t *slot;
    char name[ROFS_NAME_MAX + 1];
    uint32_t mode;
    int err;

    if (fs == NULL || path == NULL || file == NULL || !fs->mounted) {
        return ROFS_ERR_STATE;
    }

    mode = flags & 3u;
    if (mode == 0u || mode > 3u) {
        return ROFS_ERR_PARAM;
    }

    rofs__lock(fs);

    err = rofs__path_parent(fs, path, &parent, name);
    if (err != ROFS_OK) {
        rofs__unlock(fs);
        return err;
    }
    if (parent.type != ROFS_TYPE_DIR) {
        rofs__unlock(fs);
        return ROFS_ERR_NOTDIR;
    }

    err = rofs__inode_lookup(fs, parent.inode, name, &info);
    if (err == ROFS_OK) {
        if (info.type == ROFS_TYPE_DIR) {
            rofs__unlock(fs);
            return ROFS_ERR_ISDIR;
        }
        if ((flags & ROFS_O_CREAT) && (flags & ROFS_O_EXCL)) {
            rofs__unlock(fs);
            return ROFS_ERR_EXIST;
        }
        if ((flags & ROFS_O_TRUNC) && (mode & 2u) != 0u) {
            /* 清空文件：走与 truncate 相同的失效流程 */
            err = inode_truncate(fs, &info, 0u);
            if (err != ROFS_OK) {
                rofs__unlock(fs);
                return err;
            }
        }
    } else if (err == ROFS_ERR_NOENT) {
        if ((flags & ROFS_O_CREAT) == 0u) {
            rofs__unlock(fs);
            return ROFS_ERR_NOENT;
        }
        err = rofs__inode_create(fs, parent.inode, name, ROFS_TYPE_FILE, &info);
        if (err != ROFS_OK) {
            rofs__unlock(fs);
            return err;
        }
    } else {
        rofs__unlock(fs);
        return err;
    }

    slot = file_alloc(fs);
    if (slot == NULL) {
        rofs__unlock(fs);
        return ROFS_ERR_NOMEM;
    }

    memset(slot, 0, sizeof(*slot));
    slot->fs = fs;
    slot->inode = info.inode;
    slot->parent = info.parent;
    slot->size = info.size;
    slot->pos = (flags & ROFS_O_APPEND) ? info.size : 0u;
    slot->version = info.version;
    slot->epoch = info.epoch;
    slot->flags = flags;
    slot->used = 1u;
    memcpy(slot->name, info.name, info.name_len + 1u);

    *file = slot;
    rofs__unlock(fs);
    return ROFS_OK;
}

/* 关闭文件：只释放句柄；数据记录在写入时已逐条提交 */
int rofs_close(rofs_file_t *file)
{
    rofs_t *fs;

    if (file == NULL || file->used == 0u) {
        return ROFS_ERR_BADF;
    }
    fs = file->fs;
    rofs__lock(fs);
    file->used = 0u;
    rofs__unlock(fs);
    return ROFS_OK;
}

/*
 * 读取文件。
 *
 * 按 Chunk 循环：
 *   - data_read 返回有效字节数；
 *   - 从 off 开始的可用部分拷贝给用户；
 *   - 不足部分补 0（空洞）；
 *   - file->pos 与返回值按“请求长度”推进，而不是按实际数据长度。
 */
rofs_ssize_t rofs_read(rofs_file_t *file, void *buffer, uint32_t size)
{
    rofs_t *fs;
    uint8_t *dst = (uint8_t *)buffer;
    uint32_t done = 0u;
    uint32_t chunk;
    uint32_t off;
    uint32_t n;
    int got;

    if (file == NULL || file->used == 0u || buffer == NULL) {
        return ROFS_ERR_BADF;
    }
    if ((file->flags & ROFS_O_RDONLY) == 0u) {
        return ROFS_ERR_BADF;
    }
    if (size == 0u) {
        return 0;
    }
    fs = file->fs;

    rofs__lock(fs);
    if (file->pos >= file->size) {
        rofs__unlock(fs);
        return 0;
    }
    if (size > file->size - file->pos) {
        size = file->size - file->pos;
    }

    while (done < size) {
        chunk = file->pos / ROFS_CHUNK_SIZE;
        off = file->pos % ROFS_CHUNK_SIZE;
        n = ROFS_CHUNK_SIZE - off;
        if (n > size - done) {
            n = size - done;
        }

        memset(fs->workbuf, 0, ROFS_CHUNK_SIZE);
        got = data_read(fs, file->inode, chunk, file->epoch,
                        fs->workbuf, ROFS_CHUNK_SIZE);
        if (got < 0) {
            rofs__unlock(fs);
            /* 已经读到部分数据时优先返回已读字节数 */
            return (done > 0u) ? (rofs_ssize_t)done : (rofs_ssize_t)got;
        }
        {
            uint32_t want = n;
            if (got > 0 && (uint32_t)got > off) {
                /* Chunk 内有数据且覆盖了当前偏移 */
                uint32_t avail = (uint32_t)got - off;
                if (avail > want) {
                    avail = want;
                }
                memcpy(dst + done, fs->workbuf + off, avail);
                if (avail < want) {
                    memset(dst + done + avail, 0, want - avail);
                }
            } else {
                memset(dst + done, 0, want);     /* 空洞 */
            }
            file->pos += want;
            done += want;
        }
    }

    rofs__unlock(fs);
    return (rofs_ssize_t)done;
}

/*
 * 写入文件。
 *
 * 循环处理每个 Chunk：
 *   - 整块覆盖（off==0 且 n==256）直接追加 DATA；
 *   - 否则读-改-写：把旧 Chunk 读入 workbuf，合并本次数据后整块写回。
 *
 * 边界处理：
 *   如果当前 Chunk 是文件逻辑大小的边界 Chunk，则 size 之后的字节
 *   一律清零，避免把历史记录中的旧字节带入新文件内容。
 *
 * 所有 Chunk 写完后，如位置超过原大小，再更新 Inode.size。
 * 中途失败时返回已写入字节数；文件大小保持旧值，不会暴露半成品。
 */
rofs_ssize_t rofs_write(rofs_file_t *file, const void *buffer, uint32_t size)
{
    rofs_t *fs;
    const uint8_t *src = (const uint8_t *)buffer;
    rofs_ii_t info;
    uint32_t done = 0u;
    uint32_t chunk;
    uint32_t off;
    uint32_t n;
    int got;
    int err;

    if (file == NULL || file->used == 0u || (buffer == NULL && size > 0u)) {
        return ROFS_ERR_BADF;
    }
    if ((file->flags & ROFS_O_WRONLY) == 0u) {
        return ROFS_ERR_BADF;
    }
    if (size == 0u) {
        return 0;
    }
    fs = file->fs;

    rofs__lock(fs);
    if (fs->readonly) {
        rofs__unlock(fs);
        return ROFS_ERR_READONLY;
    }
    if ((file->flags & ROFS_O_APPEND) != 0u) {
        file->pos = file->size;
    }

    while (done < size) {
        chunk = file->pos / ROFS_CHUNK_SIZE;
        off = file->pos % ROFS_CHUNK_SIZE;
        n = ROFS_CHUNK_SIZE - off;
        if (n > size - done) {
            n = size - done;
        }

        if (off == 0u && n == ROFS_CHUNK_SIZE) {
            /* 整块写：无需读取旧数据 */
            err = data_write(fs, file->inode, chunk, file->epoch,
                             src + done, n);
        } else {
            uint32_t old_len = 0u;
            uint32_t new_len;

            memset(fs->workbuf, 0, ROFS_CHUNK_SIZE);
            if (chunk < (file->size + ROFS_CHUNK_SIZE - 1u) / ROFS_CHUNK_SIZE) {
                /* Chunk 完全或部分落在当前文件大小内，需要读旧数据 */
                got = data_read(fs, file->inode, chunk, file->epoch,
                                fs->workbuf, ROFS_CHUNK_SIZE);
                if (got < 0) {
                    rofs__unlock(fs);
                    return (done > 0u) ? (rofs_ssize_t)done : (rofs_ssize_t)got;
                }
                old_len = (uint32_t)got;
                /* 边界 Chunk：逻辑大小之后的字节不能从旧记录继承 */
                if (chunk == file->size / ROFS_CHUNK_SIZE &&
                    (file->size % ROFS_CHUNK_SIZE) != 0u) {
                    uint32_t valid = file->size % ROFS_CHUNK_SIZE;
                    if (valid < old_len) {
                        old_len = valid;
                    }
                    memset(fs->workbuf + valid, 0, ROFS_CHUNK_SIZE - valid);
                }
            }
            memcpy(fs->workbuf + off, src + done, n);
            new_len = old_len;
            if (off + n > new_len) {
                new_len = off + n;
            }
            err = data_write(fs, file->inode, chunk, file->epoch,
                             fs->workbuf, new_len);
        }

        if (err != ROFS_OK) {
            rofs__unlock(fs);
            return (done > 0u) ? (rofs_ssize_t)done : (rofs_ssize_t)err;
        }

        file->pos += n;
        done += n;
    }

    /* 文件变大：追加一条 INODE_UPDATE 提交新的 size */
    if (file->pos > file->size) {
        err = rofs__inode_find(fs, file->inode, &info);
        if (err == ROFS_OK) {
            info.size = file->pos;
            err = rofs__inode_update(fs, &info);
        }
        if (err == ROFS_OK) {
            file->size = info.size;
            file->version = info.version;
            file->epoch = info.epoch;
        }
    }

    rofs__unlock(fs);
    return (rofs_ssize_t)done;
}

/*
 * 定位文件位置。
 * 使用 64 位中间值计算，避免负数/大偏移的溢出；
 * 允许定位到文件尾之后（后续读空洞补 0，写则形成空洞）。
 */
int rofs_seek(rofs_file_t *file, rofs_off_t offset, int whence)
{
    int64_t base;
    int64_t pos;

    if (file == NULL || file->used == 0u) {
        return ROFS_ERR_BADF;
    }

    if (whence == ROFS_SEEK_SET) {
        base = 0;
    } else if (whence == ROFS_SEEK_CUR) {
        base = (int64_t)file->pos;
    } else if (whence == ROFS_SEEK_END) {
        base = (int64_t)file->size;
    } else {
        return ROFS_ERR_PARAM;
    }

    pos = base + (int64_t)offset;
    if (pos < 0 || pos > 0x7FFFFFFF) {
        return ROFS_ERR_INVAL;
    }

    rofs__lock(file->fs);
    file->pos = (uint32_t)pos;
    rofs__unlock(file->fs);
    return ROFS_OK;
}

/* 返回当前读写位置 */
rofs_off_t rofs_tell(rofs_file_t *file)
{
    if (file == NULL || file->used == 0u) {
        return ROFS_ERR_BADF;
    }
    return (rofs_off_t)file->pos;
}

/* 返回当前文件大小；句柄无效返回 0 */
rofs_size_t rofs_size(rofs_file_t *file)
{
    if (file == NULL || file->used == 0u) {
        return 0u;
    }
    return file->size;
}

/*
 * 截断文件（仅支持缩小）。
 * 需要写权限；成功后同步更新句柄中的 size/version/pos。
 */
int rofs_truncate(rofs_file_t *file, rofs_size_t size)
{
    rofs_ii_t info;
    int err;

    if (file == NULL || file->used == 0u) {
        return ROFS_ERR_BADF;
    }
    if ((file->flags & ROFS_O_WRONLY) == 0u) {
        return ROFS_ERR_BADF;
    }
    if (size > file->size) {
        return ROFS_ERR_NOTSUP;      /* V1 仅支持缩小，扩展请用 write */
    }
    if (size == file->size) {
        return ROFS_OK;
    }

    rofs__lock(file->fs);
    err = rofs__inode_find(file->fs, file->inode, &info);
    if (err == ROFS_OK) {
        err = inode_truncate(file->fs, &info, size);
    }
    if (err == ROFS_OK) {
        file->size = size;
        file->version = info.version;
        if (file->pos > size) {
            file->pos = size;
        }
    }
    rofs__unlock(file->fs);
    return err;
}

/* 同步底层介质；未挂载时静默返回成功 */
int rofs_fsync(rofs_file_t *file)
{
    if (file == NULL || file->used == 0u) {
        return ROFS_ERR_BADF;
    }
    rofs__lock(file->fs);
    if (file->fs->mounted) {
        (void)rofs__sync(file->fs);
    }
    rofs__unlock(file->fs);
    return ROFS_OK;
}
