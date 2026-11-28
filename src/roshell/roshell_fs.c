/*
 * roshell_fs.c - 直接执行文件系统中的脚本（流式，不整读）
 *
 * 直接使用 VFS 接口（vfs_stat / vfs_open / vfs_seek / vfs_read / vfs_close），
 * 不引入额外的回调抽象：
 *   1. vfs_stat 取文件大小并做前置校验（目录/空文件）；
 *   2. 一遍扫描记录每行首字节偏移（4B/行，与文件大小无关）；
 *   3. 引擎按行回调取行（seek + 逐字节读到行尾），扫描校验后执行。
 *
 * 行数上限 RS_LINE_SRC_MAX（默认 256），单行长度上限 RS_LINE_MAX。
 */

#include "roshell_fs.h"
#include "roshell_interp.h"
#include "vfs.h"
#include "fs.h"
#include "ros.h"

#include <string.h>

#ifndef RS_FILE_LINES_MAX
#define RS_FILE_LINES_MAX   RS_LINE_SRC_MAX  /* 行索引上限（行数，不是字节数） */
#endif

/* 行取源上下文：打开的文件 + 行偏移表 */
typedef struct {
    vfs_file_t      file;
    const uint32_t *off;                    /* 每行首字节偏移 */
} rs_file_src_t;

/* 读取第 idx 行：定位到行首，逐字节读到最后换行前（\r 去掉） */
static int32_t rs_file_line(void *ctx, uint16_t idx, char *buf, uint32_t cap)
{
    rs_file_src_t *s = (rs_file_src_t *)ctx;
    uint32_t n = 0U;

    if (s == NULL || s->off == NULL || buf == NULL || cap < 2U) {
        return -1;
    }
    if (vfs_seek(&s->file, (int32_t)s->off[idx], FS_SEEK_SET) != FS_OK) {
        return -1;
    }
    for (;;) {
        char ch;

        if (vfs_read(&s->file, &ch, 1U) != 1) {
            break;                          /* EOF：行尾即文件尾 */
        }
        if (ch == '\n') {
            break;
        }
        if (n + 1U >= cap) {
            return -2;                      /* 行超长：引擎报 line too long */
        }
        buf[n++] = ch;
    }
    if (n > 0U && buf[n - 1U] == '\r') {
        n--;                                /* CRLF：去掉回车 */
    }
    buf[n] = '\0';
    return (int32_t)n;
}

/*
 * 一遍扫描建行偏移表：返回行数（>0），失败返回负的错误码。
 * 末尾换行不额外产生一行（与 rs_scan 的计数规则一致）。
 */
static int32_t rs_build_index(vfs_file_t *f, uint32_t size, uint32_t *off,
                              uint16_t cap)
{
    uint32_t pos = 0U;
    uint32_t lines = 1U;

    off[0] = 0U;
    if (vfs_seek(f, 0, FS_SEEK_SET) != FS_OK) {
        return RS_ERR_NOT_FOUND;
    }
    while (pos < size) {
        char ch;

        if (vfs_read(f, &ch, 1U) != 1) {
            break;
        }
        pos++;
        if (ch == '\n' && pos < size) {
            if (lines >= (uint32_t)cap) {
                return RS_ERR_LIMIT;
            }
            off[lines++] = pos;
        }
    }
    return (int32_t)lines;
}

int rs_exec_lines(roshell_t *rs, rs_line_src_t src, void *ctx, uint16_t lines,
                  rs_err_t *err)
{
    if (rs == NULL || src == NULL || lines == 0U) {
        rs_err0(err, RS_ERR_PARAM, "bad parameter");
        return RS_ERR_PARAM;
    }
    return rs_exec_src(rs, src, ctx, lines, err);
}

int rs_file_check(const char *path, uint32_t *size, rs_err_t *err)
{
    fs_stat_t st;

    if (path == NULL) {
        rs_err0(err, RS_ERR_PARAM, "bad parameter");
        return RS_ERR_PARAM;
    }
    memset(&st, 0, sizeof(st));
    if (vfs_stat(path, &st) != FS_OK) {
        rs_err0(err, RS_ERR_NOT_FOUND, "not found");
        return RS_ERR_NOT_FOUND;
    }
    if (st.type == FS_TYPE_DIR) {
        rs_err0(err, RS_ERR_PARAM, "is a directory");
        return RS_ERR_PARAM;
    }
    if (st.size == 0U) {
        rs_err0(err, RS_ERR_PARAM, "empty script");
        return RS_ERR_PARAM;
    }
    if (size != NULL) {
        *size = st.size;
    }
    return RS_OK;
}

int rs_exec_file(roshell_t *rs, const char *path, rs_err_t *err)
{
    vfs_file_t f;
    uint32_t *off;
    uint32_t size = 0U;
    uint16_t lines;
    int32_t n;
    int rc;

    if (rs == NULL || path == NULL) {
        rs_err0(err, RS_ERR_PARAM, "bad parameter");
        return RS_ERR_PARAM;
    }
    /* 前置校验：存在 / 非目录 / 非空，并取到字节数 */
    rc = rs_file_check(path, &size, err);
    if (rc != RS_OK) {
        return rc;
    }

    /* 行偏移表按上限一次分配：4 * 256 = 1KB，与脚本字节数无关 */
    off = (uint32_t *)ros_mem_malloc((size_t)RS_FILE_LINES_MAX * sizeof(uint32_t));
    if (off == NULL) {
        rs_err0(err, RS_ERR_RESOURCE, "out of memory");
        return RS_ERR_RESOURCE;
    }

    memset(&f, 0, sizeof(f));
    rc = vfs_open(path, FS_O_RDONLY, &f);
    if (rc != FS_OK) {
        ros_mem_free(off);
        rs_err0(err, RS_ERR_NOT_FOUND, "open failed");
        return RS_ERR_NOT_FOUND;
    }

    n = rs_build_index(&f, size, off, (uint16_t)RS_FILE_LINES_MAX);
    if (n < 0) {
        (void)vfs_close(&f);
        ros_mem_free(off);
        rs_err0(err, (int)n, ((int)n == RS_ERR_LIMIT) ? "too many lines"
                                                      : "read failed");
        return (int)n;
    }
    lines = (uint16_t)n;

    {
        rs_file_src_t src;

        src.file = f;
        src.off = off;
        rc = rs_exec_src(rs, rs_file_line, &src, lines, err);
    }
    (void)vfs_close(&f);
    ros_mem_free(off);
    return rc;
}
