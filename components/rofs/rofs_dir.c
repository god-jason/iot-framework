/*
 * rofs_dir.c - 基于路径前缀的目录遍历
 * ============================================================================
 *
 * ROFS 不保存目录节点，目录树由记录中的完整相对路径隐式表达：
 *   config/device.json
 *   config/network.json
 *   config/backup/data.json
 *   www/index.html
 *
 * 遍历 "config" 时返回 device.json、network.json、backup/（同名子目录只返回一次）。
 * 为了去重，镜像记录建议按路径字典序排列（tools/make_rofs.py 已排序），
 * 这样同一子目录的所有记录相邻，使用“上一次返回名称”即可过滤重复项。
 */

#include "rofs_internal.h"

/* 目录是否存在：根目录始终存在；否则要求存在 DIR 记录或路径前缀 */
int rofs__dir_exists(rofs_t *fs, const char *dir)
{
    uint32_t off;
    uint32_t len;
    uint32_t i;

    if (fs == NULL || dir == NULL || fs->mounted == 0u) {
        return ROFS_ERR_INVALID;
    }
    if (dir[0] == '\0') {
        return ROFS_OK;                  /* 根目录 */
    }
    len = (uint32_t)strlen(dir);

    off = fs->records_offset;
    for (i = 0u; i < fs->record_count; i++) {
        rofs_rec_t rec;
        char name[ROFS_MAX_NAME + 1];
        int err = rofs__record_load(fs, off, &rec, NULL);

        if (err != ROFS_OK) {
            return err;
        }
        err = rofs__rec_name(fs, off, &rec, name, sizeof(name));
        if (err != ROFS_OK) {
            return err;
        }
        if (rec.type == ROFS_TYPE_DIR &&
            (uint32_t)strlen(name) == len && memcmp(name, dir, len) == 0) {
            return ROFS_OK;              /* 显式目录记录 */
        }
        if (rec.name_len > len && memcmp(name, dir, len) == 0 &&
            name[len] == '/') {
            return ROFS_OK;              /* 存在子项 */
        }
        off = off + ROFS_RECORD_SIZE + rec.name_len + rec.size;
    }
    return ROFS_ERR_NOT_FOUND;
}

int rofs_opendir(rofs_t *fs, const char *path, rofs_dir_t *dir)
{
    char norm[ROFS_PATH_MAX + 1];
    int err;

    if (fs == NULL || dir == NULL || fs->mounted == 0u) {
        return ROFS_ERR_INVALID;
    }
    err = rofs__path_dir(path, norm, sizeof(norm));
    if (err != ROFS_OK) {
        return err;
    }
    err = rofs__dir_exists(fs, norm);
    if (err != ROFS_OK) {
        return err;
    }

    memset(dir, 0, sizeof(*dir));
    dir->fs = fs;
    memcpy(dir->path, norm, strlen(norm) + 1u);
    dir->scan_offset = fs->records_offset;
    dir->scan_count = 0u;
    dir->last_len = 0u;
    dir->used = 1u;
    return ROFS_OK;
}

int rofs_readdir(rofs_dir_t *dir, rofs_dirent_t *entry)
{
    rofs_t *fs;
    uint32_t plen;

    if (dir == NULL || dir->used == 0u || dir->fs == NULL ||
        entry == NULL || dir->fs->mounted == 0u) {
        return ROFS_ERR_INVALID;
    }
    fs = dir->fs;
    plen = (uint32_t)strlen(dir->path);

    while (dir->scan_count < fs->record_count) {
        rofs_rec_t rec;
        char name[ROFS_MAX_NAME + 1];
        const char *cand;
        const char *slash;
        uint32_t seg_len;
        uint8_t type;
        uint32_t size;
        int err;

        err = rofs__record_load(fs, dir->scan_offset, &rec, NULL);
        if (err != ROFS_OK) {
            return err;
        }
        err = rofs__rec_name(fs, dir->scan_offset, &rec, name, sizeof(name));
        if (err != ROFS_OK) {
            return err;
        }
        dir->scan_offset += ROFS_RECORD_SIZE + rec.name_len + rec.size;
        dir->scan_count++;

        /* 取相对当前目录的候选路径 */
        if (plen == 0u) {
            cand = name;
        } else {
            if (rec.name_len <= plen || memcmp(name, dir->path, plen) != 0 ||
                name[plen] != '/') {
                continue;
            }
            cand = &name[plen + 1u];
        }
        if (cand[0] == '\0') {
            continue;
        }

        /* 还有 '/' 表示为子目录，只返回第一段 */
        slash = strchr(cand, '/');
        if (slash != NULL) {
            seg_len = (uint32_t)(slash - cand);
            type = ROFS_TYPE_DIR;
            size = 0u;
        } else {
            seg_len = (uint32_t)strlen(cand);
            type = rec.type;
            size = (type == ROFS_TYPE_FILE) ? rec.size : 0u;
        }
        if (seg_len == 0u || seg_len > ROFS_MAX_NAME) {
            continue;
        }

        /* 连续重复的子目录名只返回一次 */
        if (dir->last_len == (uint8_t)seg_len &&
            memcmp(dir->last, cand, seg_len) == 0) {
            continue;
        }

        memcpy(dir->last, cand, seg_len);
        dir->last[seg_len] = '\0';
        dir->last_len = (uint8_t)seg_len;

        memset(entry, 0, sizeof(*entry));
        entry->type = type;
        entry->size = size;
        memcpy(entry->name, cand, seg_len);
        entry->name[seg_len] = '\0';
        return 0;
    }

    return 1;                            /* 遍历结束 */
}

int rofs_rewinddir(rofs_dir_t *dir)
{
    if (dir == NULL || dir->used == 0u || dir->fs == NULL) {
        return ROFS_ERR_INVALID;
    }
    dir->scan_offset = dir->fs->records_offset;
    dir->scan_count = 0u;
    dir->last_len = 0u;
    return ROFS_OK;
}

int rofs_closedir(rofs_dir_t *dir)
{
    if (dir == NULL || dir->used == 0u) {
        return ROFS_ERR_INVALID;
    }
    dir->used = 0u;
    dir->fs = NULL;
    return ROFS_OK;
}
