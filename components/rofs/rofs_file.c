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

/*
 * 单个路径段合法性检查。
 * 空段（连续 '/'）、"."、".." 都视为非法。
 */
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

/*
 * 校验镜像内相对路径。
 *
 * 规则（与 tools/make_rofs.py 保持一致）：
 *   - 不允许以 '/' 开头或结尾；
 *   - 不允许空段（"a//b"）、"."、".."；
 *   - 总长度不超过 ROFS_PATH_MAX（255 字节）。
 *
 * allow_root 用于 opendir：允许 "" 或 "/" 表示根目录。
 * 注意：打开文件时传 allow_root=0，根目录不能作为文件打开。
 */
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

/*
 * 归一化目录路径。
 *
 * 与 rofs__path_check 的区别：
 *   - 接受 "/"、"config"、"/config"、"config/" 等写法；
 *   - 去掉前导与结尾 '/'，根目录输出空串；
 *   - 仍然拒绝空段、"."、".."。
 *
 * 输出的 out 用于 opendir 保存当前目录前缀，以及 readdir 做前缀匹配。
 */
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

/*
 * 读取并解析 Record Header。
 *
 * 仅做“16 字节必须落在镜像内”的检查；name_len/size 的边界校验由
 * rofs__record_load 完成，避免每个调用点重复实现。
 */
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

/*
 * 读取记录的文件名。
 *
 * 文件名紧跟 Record Header，长度由 name_len 指定，不带 '\0'。
 * buf 必须至少 cap >= name_len + 1，函数负责补 '\0'。
 * 只在确实需要比较/展示名称时调用，避免不必要的介质读取。
 */
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

/*
 * 读取记录头、校验边界并计算 Body 偏移。
 *
 * 校验内容：
 *   - 记录头完整位于记录区内；
 *   - name_len 非 0，type 合法（FILE/DIR）；
 *   - Name 与 Body 均不越过记录区末尾。
 *
 * 这是顺序扫描的公共入口：lookup、readdir、dir_exists 都通过它
 * 计算下一条记录的偏移，确保损坏字段不会导致越界访问。
 */
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

/*
 * 按相对路径顺序查找记录。
 *
 * 算法：
 *   for 每条记录：
 *       读取并校验记录边界；
 *       只有当 name_len 与路径长度相同时才读取文件名并比较；
 *       命中则返回记录头与 Body 偏移；
 *       否则 off = Body 偏移 + size。
 *
 * 由于镜像按路径排序，查找是 O(n) 的顺序扫描；对小容量资源镜像足够。
 * 名称比较区分大小写，且路径分隔符固定为 '/'。
 */
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

/*
 * 打开文件（只读）。
 *
 * 处理流程：
 *   1. 路径合法性检查（拒绝 "/" 开头、"."、".." 等）；
 *   2. 顺序查找记录；
 *   3. 若没有精确匹配但存在该路径前缀，说明目标是隐式目录，
 *      返回 ROFS_ERR_IS_DIR；
 *   4. 记录类型必须是 FILE。
 *
 * 成功后句柄只保存 Body 偏移、大小与当前位置，不保存文件名，
 * 因此打开后再次改名/换镜像不会越界（镜像本身只读）。
 */
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

/*
 * 关闭文件句柄。
 * 只清除 used 标志，不触发任何介质操作。
 */
int rofs_close(rofs_file_t *file)
{
    if (file == NULL || file->used == 0u) {
        return ROFS_ERR_INVALID;
    }
    file->used = 0u;
    file->fs = NULL;
    return ROFS_OK;
}

/*
 * 读取文件数据。
 *
 * 返回：
 *   >0 : 实际读取字节数（已按文件大小裁剪）；
 *    0 : 已到 EOF 或请求长度为 0；
 *   <0 : ROFS_ERR_* 错误码。
 *
 * 读取范围始终限制在 [Body, Body+size) 内，不依赖调用者的 seek 历史。
 */
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

/*
 * 定位读取位置。
 *
 * whence 取 SEEK_SET / SEEK_CUR / SEEK_END；
 * 目标位置必须落在 [0, size] 内，越界返回 ROFS_ERR_RANGE。
 * 只读文件不允许 seek 到文件尾之后形成稀疏区域。
 */
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

/* 返回当前读取位置；句柄无效时返回 0 */
uint32_t rofs_tell(rofs_file_t *file)
{
    return (file != NULL && file->used != 0u) ? file->position : 0u;
}

/* 返回文件大小；句柄无效时返回 0 */
uint32_t rofs_size(rofs_file_t *file)
{
    return (file != NULL && file->used != 0u) ? file->size : 0u;
}

/* 是否到达文件尾：1=已到 EOF（含句柄无效），0=未到 */
int rofs_eof(rofs_file_t *file)
{
    if (file == NULL || file->used == 0u) {
        return 1;
    }
    return (file->position >= file->size) ? 1 : 0;
}

/*
 * 判断路径是否存在。
 * 返回 1=存在（文件或隐式目录），0=不存在，负值=路径非法/介质错误。
 */
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

/*
 * 查询文件属性。
 *
 * 只支持对实际记录（文件或显式 DIR 记录）stat；
 * 隐式目录（仅通过路径前缀存在）没有对应记录，会返回 NOT_FOUND。
 * stat.name 填充调用者传入的相对路径。
 */
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
