/*
 * mfs_blockdev.c - 块设备回调封装
 * ============================================================================
 *
 * 所有下层访问都经过这一层，作用：
 *   - 统一检查 fs/回调/缓冲区参数；
 *   - 把底层非 0 返回值映射为 MFS_ERR_IO；
 *   - 允许 len 为 0 的调用（便于拼接 Payload）。
 *
 * 本层不负责加锁：锁由公开 API（mfs_lock/mfs_unlock）负责，
 * 避免在块设备回调中重复加锁导致非递归互斥量死锁。
 */

#include "mfs_internal.h"

/* 读取；len 为 0 时直接成功 */
int mfs_dev_read(mfs_t *fs, uint32_t addr, void *buf, uint32_t len)
{
    if (fs == NULL || fs->dev.read == NULL) {
        return MFS_ERR_PARAM;
    }
    if (len == 0u) {
        return MFS_OK;
    }
    if (buf == NULL) {
        return MFS_ERR_PARAM;
    }
    if (fs->dev.read(fs->dev.ctx, addr, buf, len) != 0) {
        return MFS_ERR_IO;
    }
    return MFS_OK;
}

/* 编程；介质只允许 1 -> 0，调用者保证地址已擦除或可追加 */
int mfs_dev_prog(mfs_t *fs, uint32_t addr, const void *buf, uint32_t len)
{
    if (fs == NULL || fs->dev.program == NULL) {
        return MFS_ERR_PARAM;
    }
    if (len == 0u) {
        return MFS_OK;
    }
    if (buf == NULL) {
        return MFS_ERR_PARAM;
    }
    if (fs->dev.program(fs->dev.ctx, addr, buf, len) != 0) {
        return MFS_ERR_IO;
    }
    return MFS_OK;
}

/* 擦除；addr/len 的块对齐由调用者保证 */
int mfs_dev_erase(mfs_t *fs, uint32_t addr, uint32_t len)
{
    if (fs == NULL || fs->dev.erase == NULL) {
        return MFS_ERR_PARAM;
    }
    if (fs->dev.erase(fs->dev.ctx, addr, len) != 0) {
        return MFS_ERR_IO;
    }
    return MFS_OK;
}

/* 同步；未提供 sync 回调时视为成功 */
int mfs_dev_sync(mfs_t *fs)
{
    if (fs == NULL) {
        return MFS_ERR_PARAM;
    }
    if (fs->dev.sync == NULL) {
        return MFS_OK;
    }
    if (fs->dev.sync(fs->dev.ctx) != 0) {
        return MFS_ERR_IO;
    }
    return MFS_OK;
}
