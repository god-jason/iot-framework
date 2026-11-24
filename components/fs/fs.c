/*
 * fs.c - 通用文件系统错误码
 * ============================================================================
 *
 * 具体后端（littlefs）返回的错误码与 fs_error_t 中常见项一致，
 * 这里统一提供字符串转换，避免上层依赖后端私有错误定义。
 */

#include "fs.h"

const char *fs_strerror(int error)
{
    switch (error) {
    case FS_OK:               return "ok";
    case FS_ERR_IO:           return "io error";
    case FS_ERR_NOENT:        return "no such entry";
    case FS_ERR_EXIST:        return "already exists";
    case FS_ERR_NOTDIR:       return "not a directory";
    case FS_ERR_ISDIR:        return "is a directory";
    case FS_ERR_NOTEMPTY:     return "not empty";
    case FS_ERR_BADF:         return "bad file handle";
    case FS_ERR_FBIG:         return "file too large";
    case FS_ERR_INVAL:        return "invalid argument";
    case FS_ERR_NOSPC:        return "no space left";
    case FS_ERR_NOMEM:        return "no memory";
    case FS_ERR_NOATTR:       return "no attribute";
    case FS_ERR_NAMETOOLONG:  return "name too long";
    case FS_ERR_CORRUPT:      return "corrupted";
    case FS_ERR_NOTSUP:       return "not supported";
    case FS_ERR_FORMAT:       return "not formatted";
    case FS_ERR_STATE:        return "invalid state";
    case FS_ERR_BUSY:         return "busy";
    default:                  return "unknown error";
    }
}
