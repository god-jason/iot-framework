/*
 * FOTA 远程固件升级模块实现。
 *
 * 职责：
 *   把「下载固件 -> 写入升级分区 -> 校验 -> 请求重启应用」串成一次
 *   可调用的升级流程，下载源与重启动作均通过回调解耦。
 *
 * 数据流：
 *   fota_run(version, size) 初始化 upgrade 会话 -> 循环调用 s_read 拉取
 *   数据 -> upgrade_write 按偏移写入 -> upgrade_verify 校验 ->
 *   upgrade_finish 标记就绪 -> s_apply() 或 boot_request_upgrade()。
 *
 * 依赖：
 *   bootloader.h（分区读写/镜像校验/请求进入 Bootloader）、
 *   log.h（日志）。
 *
 * 静态状态与边界：
 *   - s_read/s_read_user/s_apply 为模块级回调，同一时刻只支持一个升级任务；
 *   - 读取使用 256 字节栈缓冲，单次最多搬运 256 字节；
 *   - size 必须大于 0 且已设置读取回调，否则直接失败；
 *   - 读到 0 字节视为数据源提前结束，跳出循环后仍会进入校验（可能失败）；
 *   - 写入/校验/完成失败时不自动 abort（写入失败路径会 abort）。
 */
#include "fota.h"
#include "bootloader.h"
#include "log.h"
#include <string.h>

/* 固件读取回调，由 fota_set_read() 注册 */
static fota_read_fn s_read = NULL;
/* 读取回调的用户参数，原样透传 */
static void *s_read_user = NULL;
/* 升级完成后的应用钩子（可选），为空时使用默认 boot_request_upgrade() */
static fota_apply_fn s_apply = NULL;

/*
 * 初始化 FOTA 模块。
 * 返回：upgrade_init() 的返回值。
 * 说明：清空全部回调，避免复用上一次升级残留的函数指针。
 */
int fota_init(void)
{
    s_read = NULL;
    s_read_user = NULL;
    s_apply = NULL;
    return upgrade_init();
}

/*
 * 注册固件数据读取回调。
 * 参数：fn 读取函数（非 NULL）；user 透传给 fn 的用户指针。
 * 返回：0 成功；fn 为 NULL 返回 -1。
 */
int fota_set_read(fota_read_fn fn, void *user)
{
    if (fn == NULL) {
        return -1;
    }
    s_read = fn;
    s_read_user = user;
    return 0;
}

/*
 * 注册升级完成后的应用钩子。
 * 参数：fn 钩子函数，可为 NULL（表示使用默认 boot_request_upgrade()）。
 * 返回：恒为 0。
 */
int fota_set_apply(fota_apply_fn fn)
{
    s_apply = fn;
    return 0;
}

/*
 * 执行一次完整升级。
 * 参数：version 新固件版本号；size 固件总字节数（必须 > 0）。
 * 返回：0 成功；读取源缺失、任一步骤失败返回 -1。
 * 说明：
 *   - 每轮按 min(256, size - offset) 请求数据，读取回调返回 0 表示
 *     数据源结束，返回负值表示错误；
 *   - 校验或完成失败时直接返回，调用者可再次发起升级；
 *   - 成功后优先调用 s_apply，否则请求 Bootloader 复位升级。
 */
int fota_run(uint32_t version, uint32_t size)
{
    upgrade_info_t info;
    uint8_t buf[256];
    uint32_t offset = 0;
    int n;

    if (s_read == NULL || size == 0) {
        LOG_E("fota", "invalid read source");   /* 失败：未配置数据源或长度为 0 */
        return -1;
    }

    memset(&info, 0, sizeof(info));
    info.version = version;
    info.size = size;

    if (upgrade_begin(&info) != 0) {
        LOG_E("fota", "upgrade begin failed");   /* 失败：升级会话初始化失败 */
        return -1;
    }
    LOG_I("fota", "downloading %u bytes", (unsigned)size);   /* 开始下载 */

    while (offset < size) {
        uint32_t max = sizeof(buf);
        /* 最后一块按剩余长度裁剪，避免越界请求 */
        if (size - offset < max) {
            max = size - offset;
        }
        n = s_read(buf, max, s_read_user);
        if (n < 0) {
            LOG_E("fota", "read error at %u", (unsigned)offset);   /* 失败：读取数据出错 */
            upgrade_abort();
            return -1;
        }
        if (n == 0) {
            break;   /* 数据源提前结束 */
        }
        if (upgrade_write(offset, buf, (uint32_t)n) != 0) {
            LOG_E("fota", "write error at %u", (unsigned)offset);   /* 失败：写入升级分区出错 */
            upgrade_abort();
            return -1;
        }
        offset += (uint32_t)n;
    }

    if (upgrade_verify() != 0) {
        LOG_E("fota", "verify failed");   /* 失败：固件校验不通过 */
        return -1;
    }
    if (upgrade_finish() != 0) {
        LOG_E("fota", "finish failed");   /* 失败：升级收尾失败 */
        return -1;
    }

    LOG_I("fota", "upgrade ready, apply");   /* 升级就绪，准备应用 */
    if (s_apply != NULL) {
        s_apply();
    } else {
        /* 默认路径：请求 Bootloader 在重启时完成镜像切换 */
        boot_request_upgrade();
    }
    return 0;
}