/*
 * rofs_file.c - 路径校验、记录查找与只读文件接口
 * ============================================================================
 *
 * 记录格式（紧凑，无填充）：
 *   Record Header(16B) + Name(name_len) + Body(size)
 *
 * 查找采用顺序扫描：每步都用当前记录的 name_len/size 计算下一条偏移，
 * 并在读取前检查是否越过记录区，避免被损坏的长度字段带出边界。
 */

#include "rofs_internal.h"

#include <stdio.h>               /* SEEK_SET / SEEK_CUR / SEEK_END */

/* ---------------- 路径 ---------------- */

/* 校验单个路径段：非空且不是 "." / ".." */
static int path_seg_ok(const char *seg, uint32_t len)
{
    if (len == 0u) {
        return 0;
    }
    if (len == 1u && seg[0] == '.') {
        return 0;
    }
    if (len == 2u && seg[0] == '.' && seg[1] == '.') {
        return 0;
    }
    return 1;
}

int rofs__path_check(const char *path, int allow_root)
{
    uint32_t len;
    uint32_t i;

    if (path == NULL) {
        return ROFS_ERR_INVALID;
    }
    len = (uint32_t)strlen(path);
    if (len == 0u) {
        return allow_root ? ROFS_OK : ROFS_ERR_INVALID;
    }
    if (len > ROFS_PATH_MAX) {
        return ROFS_ERR_INVALID;
    }
    if (allow_root && len == 1u && path[0] == '/') {
        return ROFS_OK;
    }
    if (path[0] == '/' || path[len - 1u] == '/') {
        return ROFS_ERR_INVALID;         /* 不允许前导/结尾 '/' */
    }

    i = 0u;
    while (i < len) {
        uint32_t start = i;

        while (i < len && path[i] != '/') {
            i++;
        }
        if (!path_seg_ok(&path[start], i - start)) {
            return ROFS_ERR_INVALID;
        }
        i++;                             /* 跳过 '/' */
    }
    return ROFS_OK;
}

int rofs__path_dir(const char *path, char *out, uint32_t cap)
{
    const char *src;
    uint32_t len;

    if (path == NULL || out == NULL || cap == 0u) {
        return ROFS_ERR_INVALID;
    }
    src = path;
    if (src[0] == '/') {
        src++;
    }
    len = (uint32_t)strlen(src);
    while (len > 0u && src[len - 1u] == '/') {
        len--;
    }
    if (len == 0u) {
        out[0] = '\0';
        return ROFS_OK;                  /* 根目录 */
    }
    if (len > ROFS_PATH_MAX || len >= cap) {
        return ROFS_ERR_INVALID;
    }

    /* 校验各段（此时首尾无 '/'） */
    {
        uint32_t i = 0u;

        while (i < len) {
            uint32_t start = i;

            while (i < len && src[i] != '/') {
                i++;
            }
            if (!path_seg_ok(&src[start], i - start)) {
                return ROFS_ERR_INVALID;
            }
            i++;
        }
    }

    memcpy(out, src, len);
    out[len] = '\0';
    return ROFS_OK;
}

/* ---------------- 记录读取 ---------------- */

int rofs__rec_read(rofs_t *fs, uint32_t rec_off, rofs_rec_t *rec)
{
    uint8_t buf[ROFS_RECORD_SIZE];
    int err;

    if (fs == NULL || rec == NULL) {
        return ROFS_ERR_PARAM;
    }
    if (rec_off > fs->image_size ||
        fs->image_size - rec_off < ROFS_RECORD_SIZE) {
        return ROFS_ERR_RANGE;
    }
    err = rofs__read(fs, rec_off, buf, sizeof(buf));
    if (err != ROFS_OK) {
        return err;
    }

    rec->type = buf[ROFS_REC_OFF_TYPE];
    rec->name_len = buf[ROFS_REC_OFF_NAME_LEN];
    rec->flags = rofs__get_u16(&buf[ROFS_REC_OFF_FLAGS]);
    rec->size = rofs__get_u32(&buf[ROFS_REC_OFF_SIZE]);
    rec->time = rofs__get_u32(&buf[ROFS_REC_OFF_TIME]);
    rec->crc = rofs__get_u32(&buf[ROFS_REC_OFF_CRC]);
    return ROFS_OK;
}

int rofs__rec_name(rofs_t *fs, uint32_t rec_off, const rofs_rec_t *rec,
                   char *buf, uint32_t cap)
{
    uint32_t n;

    if (fs == NULL || rec == NULL || buf == NULL || rec->name_len == 0u) {
        return ROFS_ERR_PARAM;
    }
    n = rec->name_len;
    if (n > ROFS_MAX_NAME || cap < n + 1u) {
        return ROFS_ERR_PARAM;
    }
    {
        int err = rofs__read(fs, rec_off + ROFS_RECORD_SIZE, buf, n);

        if (err != ROFS_OK) {
            return err;
        }
    }
    buf[n] = '\0';
    return ROFS_OK;
}

/* 读取记录头并计算 Body 偏移，同时校验记录边界 */
int rofs__record_load(rofs_t *fs, uint32_t off, rofs_rec_t *rec,
                      uint32_t *body_off)
{
    uint32_t end = rofs__records_end(fs);
    uint32_t body;
    int err;

    if (off > end || end - off < ROFS_RECORD_SIZE) {
        return ROFS_ERR_FORMAT;
    }
    err = rofs__rec_read(fs, off, rec);
    if (err != ROFS_OK) {
        return err;
    }
    if (rec->name_len == 0u ||
        (rec->type != ROFS_TYPE_FILE && rec->type != ROFS_TYPE_DIR)) {
        return ROFS_ERR_FORMAT;
    }
    body = off + ROFS_RECORD_SIZE + rec->name_len;
    if (body > end || rec->size > end - body) {
        return ROFS_ERR_FORMAT;
    }
    if (body_off != NULL) {
        *body_off = body;
    }
    return ROFS_OK;
}

int rofs__lookup(rofs_t *fs, const char *path, rofs_rec_t *rec,
                 uint32_t *body_off)
{
    uint32_t off;
    uint32_t path_len;
    uint32_t i;
    char name[ROFS_MAX_NAME + 1];
    int err;

    if (fs == NULL || path == NULL || rec == NULL || fs->mounted == 0u) {
        return ROFS_ERR_INVALID;
    }
    path_len = (uint32_t)strlen(path);
    if (path_len == 0u || path_len > ROFS_MAX_NAME) {
        return ROFS_ERR_INVALID;
    }

    off = fs->records_offset;

    for (i = 0u; i < fs->record_count; i++) {
        uint32_t body = 0u;

        err = rofs__record_load(fs, off, rec, &body);
        if (err != ROFS_OK) {
            return err;
        }
        if (rec->name_len == path_len) {
            err = rofs__rec_name(fs, off, rec, name, sizeof(name));
            if (err != ROFS_OK) {
                return err;
            }
            if (memcmp(name, path, path_len) == 0) {
                if (body_off != NULL) {
                    *body_off = body;
                }
                return ROFS_OK;
            }
        }
        off = body + rec->size;
    }

    return ROFS_ERR_NOT_FOUND;
}

/* ---------------- 文件接口 ---------------- */

int rofs_open(rofs_t *fs, const char *path, rofs_file_t *file)
{
    rofs_rec_t rec;
    uint32_t body = 0u;
    int err;

    if (fs == NULL || file == NULL || fs->mounted == 0u) {
        return ROFS_ERR_INVALID;
    }
    err = rofs__path_check(path, 0);
    if (err != ROFS_OK) {
        return err;
    }
    err = rofs__lookup(fs, path, &rec, &body);
    if (err == ROFS_ERR_NOT_FOUND) {
        /* 目录由路径前缀隐式表达，不存在对应文件记录 */
        return (rofs__dir_exists(fs, path) == ROFS_OK)
                   ? ROFS_ERR_IS_DIR : ROFS_ERR_NOT_FOUND;
    }
    if (err != ROFS_OK) {
        return err;
    }
    if (rec.type != ROFS_TYPE_FILE) {
        return ROFS_ERR_IS_DIR;
    }

    memset(file, 0, sizeof(*file));
    file->fs = fs;
    file->offset = body;
    file->size = rec.size;
    file->position = 0u;
    file->used = 1u;
    return ROFS_OK;
}

int rofs_close(rofs_file_t *file)
{
    if (file == NULL || file->used == 0u) {
        return ROFS_ERR_INVALID;
    }
    file->used = 0u;
    file->fs = NULL;
    return ROFS_OK;
}

int32_t rofs_read(rofs_file_t *file, void *buffer, uint32_t size)
{
    uint32_t n;
    int err;

    if (file == NULL || file->used == 0u || file->fs == NULL ||
        file->fs->mounted == 0u) {
        return ROFS_ERR_INVALID;
    }
    if (buffer == NULL && size != 0u) {
        return ROFS_ERR_PARAM;
    }
    if (size == 0u || file->position >= file->size) {
        return 0;                        /* EOF */
    }

    n = file->size - file->position;
    if (size < n) {
        n = size;
    }
    err = rofs__read(file->fs, file->offset + file->position, buffer, n);
    if (err != ROFS_OK) {
        return err;
    }
    file->position += n;
    return (int32_t)n;
}

int rofs_seek(rofs_file_t *file, int32_t offset, int whence)
{
    int64_t base;
    int64_t target;

    if (file == NULL || file->used == 0u) {
        return ROFS_ERR_INVALID;
    }

    if (whence == SEEK_SET) {
        base = 0;
    } else if (whence == SEEK_CUR) {
        base = (int64_t)file->position;
    } else if (whence == SEEK_END) {
        base = (int64_t)file->size;
    } else {
        return ROFS_ERR_PARAM;
    }

    target = base + (int64_t)offset;
    if (target < 0 || target > (int64_t)file->size) {
        return ROFS_ERR_RANGE;
    }
    file->position = (uint32_t)target;
    return ROFS_OK;
}

uint32_t rofs_tell(rofs_file_t *file)
{
    return (file != NULL && file->used != 0u) ? file->position : 0u;
}

uint32_t rofs_size(rofs_file_t *file)
{
    return (file != NULL && file->used != 0u) ? file->size : 0u;
}

int rofs_eof(rofs_file_t *file)
{
    if (file == NULL || file->used == 0u) {
        return 1;
    }
    return (file->position >= file->size) ? 1 : 0;
}

int rofs_exists(rofs_t *fs, const char *path)
{
    rofs_rec_t rec;
    int err;

    if (rofs__path_check(path, 0) != ROFS_OK) {
        return ROFS_ERR_INVALID;
    }
    err = rofs__lookup(fs, path, &rec, NULL);
    if (err == ROFS_OK) {
        return 1;
    }
    return (err == ROFS_ERR_NOT_FOUND) ? 0 : err;
}

int rofs_stat(rofs_t *fs, const char *path, rofs_stat_t *stat)
{
    rofs_rec_t rec;
    int err;

    if (stat == NULL) {
        return ROFS_ERR_PARAM;
    }
    err = rofs__path_check(path, 0);
    if (err != ROFS_OK) {
        return err;
    }
    err = rofs__lookup(fs, path, &rec, NULL);
    if (err != ROFS_OK) {
        return err;
    }

    memset(stat, 0, sizeof(*stat));
    stat->type = rec.type;
    stat->size = (rec.type == ROFS_TYPE_FILE) ? rec.size : 0u;
    stat->time = rec.time;
    memcpy(stat->name, path, strlen(path) + 1u);
    return ROFS_OK;
}
