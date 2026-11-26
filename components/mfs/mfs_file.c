/*
 * mfs_file.c - 文件打开、读写、Seek 与截断
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

#include "mfs_internal.h"

/*
 * 读取一个 Chunk 的有效数据到 buf。
 * 最新 DATA 记录的查找复用 mfs_data_locate（与 GC 判定逻辑一致）。
 * 返回：
 *   正数：实际字节数；
 *   0   ：空洞（没有有效数据，调用者补 0）；
 *   负数：错误码。
 */
static int data_read(mfs_t *fs, uint32_t inode, uint32_t chunk,
                     uint32_t epoch, uint8_t *buf, uint32_t cap,
                     uint32_t *hint)
{
    mfs_data_loc_t loc;
    mfs_rec_hdr_t hdr;
    uint32_t next;
    uint32_t size;
    int err;

    /*
     * 顺序读快通道：顺序写入时下一个 Chunk 的 DATA 记录通常紧跟在上一条之后。
     * 先按 hint 探测该位置，命中则省去一次全盘 locate（大文件顺序读从
     * O(Chunk×日志) 降到近似 O(Chunk)）。
     */
    if (hint != NULL && *hint != 0u) {
        err = mfs_rec_read_fast(fs, *hint, &hdr, &next);
        if (err == MFS_OK && hdr.type == MFS_REC_DATA &&
            hdr.object_id == inode &&
            hdr.payload_size >= MFS_DATA_PREFIX &&
            hdr.payload_size <= MFS_DATA_PREFIX + MFS_CHUNK_SIZE) {
            uint8_t pre[MFS_DATA_PREFIX];

            if (mfs_rec_read_raw(fs, *hint, &hdr, 0u, pre, sizeof(pre)) == MFS_OK &&
                mfs_get_u32(&pre[4]) == chunk &&
                mfs_get_u32(&pre[12]) == epoch) {
                size = hdr.payload_size - MFS_DATA_PREFIX;
                if (size > cap) {
                    size = cap;
                }
                err = mfs_rec_read_raw(fs, *hint, &hdr,
                                        MFS_DATA_PREFIX, buf, size);
                if (err == MFS_OK) {
                    *hint = next;
                    return (int)size;
                }
            }
        }
    }

    err = mfs_data_locate(fs, inode, chunk, epoch, &loc);
    if (err != MFS_OK) {
        return err;
    }
    if (!loc.found || loc.size == 0u || loc.seq < loc.del_seq) {
        if (hint != NULL) {
            *hint = 0u;
        }
        return 0;                        /* 无数据或被截断失效 */
    }

    /* 扫描阶段已校验过 CRC，这里再确认一次记录完整性 */
    err = mfs_rec_read(fs, loc.addr, &hdr, NULL, 0u, &next);
    if (err != MFS_OK) {
        return err;
    }
    if (hint != NULL) {
        *hint = next;                    /* 下一条记录，供顺序读探测 */
    }

    size = loc.size;
    if (size > cap) {
        size = cap;
    }
    /* 只读取 Payload 的数据部分，跳过 24B 前缀 */
    err = mfs_rec_read_raw(fs, loc.addr, &hdr, MFS_DATA_PREFIX, buf, size);
    if (err != MFS_OK) {
        return err;
    }
    return (int)size;
}

/*
 * 写入一个 Chunk（最多 256B）。
 * generation 使用当前 fs->seq，保证后写入的记录一定更新。
 */
static int data_write(mfs_t *fs, uint32_t inode, uint32_t chunk,
                      uint32_t epoch, const void *data, uint32_t size)
{
    uint8_t prefix[MFS_DATA_PREFIX];

    if (size > MFS_CHUNK_SIZE) {
        return MFS_ERR_PARAM;
    }

    mfs_put_u32(&prefix[0], inode);
    mfs_put_u32(&prefix[4], chunk);
    mfs_put_u32(&prefix[8], fs->seq);      /* generation */
    mfs_put_u32(&prefix[12], epoch);
    mfs_put_u16(&prefix[16], (uint16_t)size);
    mfs_put_u16(&prefix[18], 0u);
    mfs_put_u32(&prefix[20], 0u);

    return mfs_rec_write(fs, MFS_REC_DATA, inode,
                           prefix, sizeof(prefix), data, size);
}

/*
 * 写一条 DATA_DELETE，失效 [start_chunk, start_chunk + count) 范围。
 * 一条记录可覆盖大量 Chunk，避免截断大文件时产生海量小记录。
 */
static int data_delete(mfs_t *fs, uint32_t inode, uint32_t epoch,
                       uint32_t start_chunk, uint32_t count)
{
    uint8_t prefix[16];

    if (count == 0u) {
        return MFS_OK;
    }
    mfs_put_u32(&prefix[0], inode);
    mfs_put_u32(&prefix[4], start_chunk);
    mfs_put_u32(&prefix[8], count);
    mfs_put_u32(&prefix[12], epoch);
    return mfs_rec_write(fs, MFS_REC_DATA_DELETE, inode,
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
static int inode_truncate(mfs_t *fs, mfs_ii_t *info, uint32_t new_size)
{
    uint32_t base;
    uint32_t valid;
    uint32_t start;
    uint32_t end;
    int got;
    int err;

    if (new_size > info->size) {
        return MFS_ERR_NOTSUP;          /* V1 不支持通过 truncate 扩展 */
    }
    if (new_size == info->size) {
        return MFS_OK;
    }

    base = new_size / MFS_CHUNK_SIZE;   /* 边界 Chunk 号 */
    valid = new_size % MFS_CHUNK_SIZE;  /* 边界 Chunk 内有效字节数 */

    /* 边界 Chunk 改写为缩短后的记录 */
    if (valid != 0u) {
        memset(fs->workbuf, 0, MFS_CHUNK_SIZE);
        got = data_read(fs, info->inode, base, info->epoch,
                        fs->workbuf, MFS_CHUNK_SIZE, NULL);
        if (got < 0) {
            return got;
        }
        err = data_write(fs, info->inode, base, info->epoch,
                         fs->workbuf, valid);
        if (err != MFS_OK) {
            return err;
        }
    }

    /* 边界之后的所有完整 Chunk 一次性失效 */
    start = (valid != 0u) ? base + 1u : base;
    end = (info->size + MFS_CHUNK_SIZE - 1u) / MFS_CHUNK_SIZE;
    if (end > start) {
        err = data_delete(fs, info->inode, info->epoch, start, end - start);
        if (err != MFS_OK) {
            return err;
        }
    }

    info->size = new_size;
    return mfs_inode_update(fs, info);
}

/* 从内部句柄池分配一个空闲文件句柄 */
static mfs_file_t *file_alloc(mfs_t *fs)
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
int mfs_open(mfs_t *fs, const char *path, uint32_t flags, mfs_file_t **file)
{
    mfs_ii_t parent;
    mfs_ii_t info;
    mfs_file_t *slot;
    char name[MFS_NAME_MAX + 1];
    uint32_t mode;
    int err;

    if (fs == NULL || path == NULL || file == NULL || !fs->mounted) {
        return MFS_ERR_STATE;
    }

    mode = flags & 3u;
    if (mode == 0u || mode > 3u) {
        return MFS_ERR_PARAM;
    }

    mfs_lock(fs);

    err = mfs_path_parent(fs, path, &parent, name);
    if (err != MFS_OK) {
        mfs_unlock(fs);
        return err;
    }
    if (parent.type != MFS_TYPE_DIR) {
        mfs_unlock(fs);
        return MFS_ERR_NOTDIR;
    }

    err = mfs_inode_lookup(fs, parent.inode, name, &info);
    if (err == MFS_OK) {
        if (info.type == MFS_TYPE_DIR) {
            mfs_unlock(fs);
            return MFS_ERR_ISDIR;
        }
        if ((flags & MFS_O_CREAT) && (flags & MFS_O_EXCL)) {
            mfs_unlock(fs);
            return MFS_ERR_EXIST;
        }
        if ((flags & MFS_O_TRUNC) && (mode & 2u) != 0u) {
            /* 清空文件：走与 truncate 相同的失效流程 */
            err = inode_truncate(fs, &info, 0u);
            if (err != MFS_OK) {
                mfs_unlock(fs);
                return err;
            }
        }
    } else if (err == MFS_ERR_NOENT) {
        if ((flags & MFS_O_CREAT) == 0u) {
            mfs_unlock(fs);
            return MFS_ERR_NOENT;
        }
        err = mfs_inode_create(fs, parent.inode, name, MFS_TYPE_FILE, &info);
        if (err != MFS_OK) {
            mfs_unlock(fs);
            return err;
        }
    } else {
        mfs_unlock(fs);
        return err;
    }

    slot = file_alloc(fs);
    if (slot == NULL) {
        mfs_unlock(fs);
        return MFS_ERR_NOMEM;
    }

    memset(slot, 0, sizeof(*slot));
    slot->fs = fs;
    slot->inode = info.inode;
    slot->parent = info.parent;
    slot->size = info.size;
    slot->pos = (flags & MFS_O_APPEND) ? info.size : 0u;
    slot->rcache = 0u;
    slot->version = info.version;
    slot->epoch = info.epoch;
    slot->flags = flags;
    slot->used = 1u;
    memcpy(slot->name, info.name, info.name_len + 1u);

    *file = slot;
    mfs_unlock(fs);
    return MFS_OK;
}

/* 关闭文件：只释放句柄；数据记录在写入时已逐条提交 */
int mfs_close(mfs_file_t *file)
{
    mfs_t *fs;

    if (file == NULL || file->used == 0u) {
        return MFS_ERR_BADF;
    }
    fs = file->fs;
    mfs_lock(fs);
    file->used = 0u;
    mfs_unlock(fs);
    return MFS_OK;
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
mfs_ssize_t mfs_read(mfs_file_t *file, void *buffer, uint32_t size)
{
    mfs_t *fs;
    uint8_t *dst = (uint8_t *)buffer;
    uint32_t done = 0u;
    uint32_t chunk;
    uint32_t off;
    uint32_t n;
    int got;

    if (file == NULL || file->used == 0u || buffer == NULL) {
        return MFS_ERR_BADF;
    }
    if ((file->flags & MFS_O_RDONLY) == 0u) {
        return MFS_ERR_BADF;
    }
    if (size == 0u) {
        return 0;
    }
    fs = file->fs;

    mfs_lock(fs);
    if (file->pos >= file->size) {
        mfs_unlock(fs);
        return 0;
    }
    if (size > file->size - file->pos) {
        size = file->size - file->pos;
    }

    while (done < size) {
        chunk = file->pos / MFS_CHUNK_SIZE;
        off = file->pos % MFS_CHUNK_SIZE;
        n = MFS_CHUNK_SIZE - off;
        if (n > size - done) {
            n = size - done;
        }

        memset(fs->workbuf, 0, MFS_CHUNK_SIZE);
        got = data_read(fs, file->inode, chunk, file->epoch,
                        fs->workbuf, MFS_CHUNK_SIZE, &file->rcache);
        if (got < 0) {
            mfs_unlock(fs);
            /* 已经读到部分数据时优先返回已读字节数 */
            return (done > 0u) ? (mfs_ssize_t)done : (mfs_ssize_t)got;
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

    mfs_unlock(fs);
    return (mfs_ssize_t)done;
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
mfs_ssize_t mfs_write(mfs_file_t *file, const void *buffer, uint32_t size)
{
    mfs_t *fs;
    const uint8_t *src = (const uint8_t *)buffer;
    mfs_ii_t info;
    uint32_t done = 0u;
    uint32_t chunk;
    uint32_t off;
    uint32_t n;
    int got;
    int err;

    if (file == NULL || file->used == 0u || (buffer == NULL && size > 0u)) {
        return MFS_ERR_BADF;
    }
    if ((file->flags & MFS_O_WRONLY) == 0u) {
        return MFS_ERR_BADF;
    }
    if (size == 0u) {
        return 0;
    }
    fs = file->fs;

    mfs_lock(fs);
    if (fs->readonly) {
        mfs_unlock(fs);
        return MFS_ERR_READONLY;
    }
    if ((file->flags & MFS_O_APPEND) != 0u) {
        file->pos = file->size;
    }
    file->rcache = 0u;               /* 写入后旧的顺序读提示可能指向过期记录 */

    while (done < size) {
        chunk = file->pos / MFS_CHUNK_SIZE;
        off = file->pos % MFS_CHUNK_SIZE;
        n = MFS_CHUNK_SIZE - off;
        if (n > size - done) {
            n = size - done;
        }

        if (off == 0u && n == MFS_CHUNK_SIZE) {
            /* 整块写：无需读取旧数据 */
            err = data_write(fs, file->inode, chunk, file->epoch,
                             src + done, n);
        } else {
            uint32_t old_len = 0u;
            uint32_t new_len;

            memset(fs->workbuf, 0, MFS_CHUNK_SIZE);
            if (chunk < (file->size + MFS_CHUNK_SIZE - 1u) / MFS_CHUNK_SIZE) {
                /* Chunk 完全或部分落在当前文件大小内，需要读旧数据 */
                got = data_read(fs, file->inode, chunk, file->epoch,
                                fs->workbuf, MFS_CHUNK_SIZE, NULL);
                if (got < 0) {
                    mfs_unlock(fs);
                    return (done > 0u) ? (mfs_ssize_t)done : (mfs_ssize_t)got;
                }
                old_len = (uint32_t)got;
                /* 边界 Chunk：逻辑大小之后的字节不能从旧记录继承 */
                if (chunk == file->size / MFS_CHUNK_SIZE &&
                    (file->size % MFS_CHUNK_SIZE) != 0u) {
                    uint32_t valid = file->size % MFS_CHUNK_SIZE;
                    if (valid < old_len) {
                        old_len = valid;
                    }
                    memset(fs->workbuf + valid, 0, MFS_CHUNK_SIZE - valid);
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

        if (err != MFS_OK) {
            /*
             * 中间某个 Chunk 写失败时，已写入的记录还没有通过 Inode 更新
             * 提交文件大小，对调用者不可见；因此返回错误而不是“部分成功”，
             * 避免调用者误以为数据已经落盘。
             */
            mfs_unlock(fs);
            return (mfs_ssize_t)err;
        }

        file->pos += n;
        done += n;
    }

    /* 文件变大：追加一条 INODE_UPDATE 提交新的 size */
    if (file->pos > file->size) {
        err = mfs_inode_find(fs, file->inode, &info);
        if (err == MFS_OK) {
            info.size = file->pos;
            err = mfs_inode_update(fs, &info);
        }
        if (err != MFS_OK) {
            mfs_unlock(fs);
            return (mfs_ssize_t)err;    /* size 未提交：本次写入不可见 */
        }
        file->size = info.size;
        file->version = info.version;
        file->epoch = info.epoch;
    }

    mfs_unlock(fs);
    return (mfs_ssize_t)done;
}

/*
 * 定位文件位置。
 * 使用 64 位中间值计算，避免负数/大偏移的溢出；
 * 允许定位到文件尾之后（后续读空洞补 0，写则形成空洞）。
 */
int mfs_seek(mfs_file_t *file, mfs_off_t offset, int whence)
{
    int64_t base;
    int64_t pos;

    if (file == NULL || file->used == 0u) {
        return MFS_ERR_BADF;
    }

    if (whence == MFS_SEEK_SET) {
        base = 0;
    } else if (whence == MFS_SEEK_CUR) {
        base = (int64_t)file->pos;
    } else if (whence == MFS_SEEK_END) {
        base = (int64_t)file->size;
    } else {
        return MFS_ERR_PARAM;
    }

    pos = base + (int64_t)offset;
    if (pos < 0 || pos > 0x7FFFFFFF) {
        return MFS_ERR_INVAL;
    }

    mfs_lock(file->fs);
    file->pos = (uint32_t)pos;
    file->rcache = 0u;               /* 位置变化后顺序读提示失效 */
    mfs_unlock(file->fs);
    return MFS_OK;
}

/* 返回当前读写位置 */
mfs_off_t mfs_tell(mfs_file_t *file)
{
    if (file == NULL || file->used == 0u) {
        return MFS_ERR_BADF;
    }
    return (mfs_off_t)file->pos;
}

/* 返回当前文件大小；句柄无效返回 0 */
mfs_size_t mfs_size(mfs_file_t *file)
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
int mfs_truncate(mfs_file_t *file, mfs_size_t size)
{
    mfs_ii_t info;
    int err;

    if (file == NULL || file->used == 0u) {
        return MFS_ERR_BADF;
    }
    if ((file->flags & MFS_O_WRONLY) == 0u) {
        return MFS_ERR_BADF;
    }
    if (size > file->size) {
        return MFS_ERR_NOTSUP;      /* V1 仅支持缩小，扩展请用 write */
    }
    if (size == file->size) {
        return MFS_OK;
    }

    mfs_lock(file->fs);
    err = mfs_inode_find(file->fs, file->inode, &info);
    if (err == MFS_OK) {
        err = inode_truncate(file->fs, &info, size);
    }
    if (err == MFS_OK) {
        file->size = size;
        file->version = info.version;
        file->rcache = 0u;
        if (file->pos > size) {
            file->pos = size;
        }
    }
    mfs_unlock(file->fs);
    return err;
}

/* 同步底层介质；未挂载时静默返回成功 */
int mfs_fsync(mfs_file_t *file)
{
    if (file == NULL || file->used == 0u) {
        return MFS_ERR_BADF;
    }
    mfs_lock(file->fs);
    if (file->fs->mounted) {
        (void)mfs_dev_sync(file->fs);
    }
    mfs_unlock(file->fs);
    return MFS_OK;
}
