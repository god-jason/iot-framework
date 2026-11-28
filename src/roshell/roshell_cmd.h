#ifndef RS_CMD_H
#define RS_CMD_H

/*
 * roshell_cmd.h - 命令定义、全局命令表与生命周期钩子
 *
 * 描述一条命令（名称 / 说明 / 参数表 / on_start / on_tick / on_stop /
 * pause-resume 钩子），并提供全局命令表的注册、查找、遍历与执行入口。
 * 参数解析见 roshell_args.h；会话与行编辑见 roshell.h。
 *
 * 命令执行状态（on_start / on_tick 返回值）：
 *   0                 命令已结束（成功）
 *   RS_CMD_RUNNING    持续命令尚未结束，需要 shell 任务继续轮转调用 on_tick
 * 其它值统一视为结束码：负值为 RS_ERR_*；正值沿用命令层
 * “返回 -RS_ERR_*”的既有约定（如 -RS_ERR_PARAM == 1）。
 * 取 0x7FFF 以避开既有正错误码（1..15）。
 */

#include <stdint.h>
#include <stdbool.h>

#include "map.h"
#include "roshell_args.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct roshell roshell_t;
struct ros_task;

enum {
    RS_CMD_RUNNING = 0x7FFF
};

#ifndef RS_CMD_NAME_MAX
#define RS_CMD_NAME_MAX        24U     /* 命令名最大长度（含结尾 0） */
#endif

/* 命令处理函数（on_start，内建与扩展命令统一签名）
 *   rs     : 会话实例（唯一上下文）
 *   argc   : 实参个数（含命令名）
 *   argv   : 实参（argv[0] 为命令名）
 *   parsed : 参数表解析结果；命令未定义参数表时为 NULL
 *   user   : def->user（为 NULL 时使用调用方默认 user） */
typedef int (*rs_cmd_fn_t)(roshell_t *rs, int argc, char **argv,
                           const rs_args_parsed_t *parsed, void *user);

/* 持续命令轮转回调（on_tick）：返回 RS_CMD_RUNNING 表示继续，其它值表示结束 */
typedef int (*rs_cmd_tick_fn_t)(roshell_t *rs, void *user);

/* 命令生命周期回调（可选）：user 为命令定义中的处理函数上下文 def->user，
 * rs 为命令所属实例（状态存于实例，如 sleep 的等待登记表） */
typedef int (*rs_cmd_hook_fn_t)(roshell_t *rs, void *user);

/* 命令定义（内建与扩展命令共用；usage 为空时帮助文本由参数表动态生成） */
typedef struct rs_cmd {
    const char *name;       /* 命令名 */
    const char *desc;       /* 一句话说明 */
    const char *usage;      /* 用法行；NULL 时由参数表动态生成（rs_cmd_usage） */
    const char *group;      /* help 分组（内建命令用）；扩展命令可为 NULL */
    const rs_args_t *args;
    uint8_t     arg_count;

    /* ---------------- 生命周期回调（按调用顺序，均可为 NULL） ---------------- */

    /* on_start：命令启动。返回 0 表示已同步结束；RS_CMD_RUNNING 表示持续命令；
     * 其它值为结束码（负值为 RS_ERR_*）。 */
    rs_cmd_fn_t on_start;

    /* on_tick：持续命令轮转。返回 RS_CMD_RUNNING 继续，其它值结束。 */
    rs_cmd_tick_fn_t on_tick;

    /* on_pause：命令所在任务被挂起前调用，返回 <0 可拒绝暂停
     * （sleep 命令约定：0=钩子已接管暂停，-1=未登记、调用方回退 OS 挂起）。 */
    rs_cmd_hook_fn_t on_pause;

    /* on_resume：任务恢复后调用。 */
    rs_cmd_hook_fn_t on_resume;

    /* on_stop：命令结束时调用一次（正常结束、on_tick 返回结束、或被停止），
     * 用于释放持续命令的登记 / 资源。 */
    rs_cmd_hook_fn_t on_stop;

    /* 回调上下文（传给上述所有回调，可为 NULL） */
    void       *user;
} rs_cmd_t;

/*
 * 生成 usage 单行文本，例如：
 *   ping <host> [--count|-c <n>] [--verbose]
 * def->usage 非 NULL 时直接输出该串，否则由参数表生成。
 * 返回写入长度；缓冲区不足返回 -1。
 */
int rs_cmd_usage(const rs_cmd_t *def, char *buf, uint32_t size);

/*
 * 生成多行帮助：说明 + usage + 参数列表。
 * 返回写入长度；缓冲区不足返回 -1。
 */
int rs_cmd_help(const rs_cmd_t *def, char *buf, uint32_t size);

/*
 * 在命令表中查找 argv[0] 并解析其余参数，解析成功后调用处理函数。
 * user 为默认上下文：命令 def->user 为 NULL 时使用它。
 * 返回处理函数返回值；未找到命令返回 RS_ARGS_ERR_UNKNOWN；
 * 解析失败返回对应 RS_ARGS_ERR_*。
 */
int rs_cmd_run(roshell_t *rs, const rs_cmd_t *table, uint32_t count,
            int argc, char **argv, void *user);

/* ============================== 全局命令表 ============================== */

/*
 * 全局命令表（定义在 roshell_cmd.c）：名称 -> const rs_cmd_t *，
 * 内建命令与扩展命令共用一张表，首次使用时惰性初始化并登记内建命令。
 * 键为命令名副本，比较区分大小写。
 */
extern map_t rs_cmd_table;

/*
 * 注册扩展命令到全局命令表（入表保存 def 指针，调用者需保证其生命周期）。
 * def->name / def->on_start 为空、名称与已有命令（含内建命令）相同时返回 -1；
 * 成功返回 0。
 */
int rs_cmd_register(const rs_cmd_t *def);

/* 清空扩展命令并重新登记内建命令（测试 / 反初始化用） */
void rs_cmd_unregister_all(void);

/* 在全局命令表中查找（含内建命令，名称区分大小写），未找到返回 NULL */
const rs_cmd_t *rs_cmd_find_command(const char *name);

/* 遍历全局命令表（含内建命令，按键名升序）：首个命令 / 后继命令 */
const rs_cmd_t *rs_cmd_first(void);
const rs_cmd_t *rs_cmd_next(const rs_cmd_t *def);

/* 解析参数并执行一条已找到的命令（argv 不含命令名，仅参数） */
int rs_cmd_run_node(roshell_t *rs, const rs_cmd_t *def, int argc, char **argv);

/*
 * 按 argv[0] 在全局命令表中查找并执行。
 * 行首 '-' 前缀（如 "-sleep 1000"）表示持续命令不等待结束：启动后立即返回，
 * 由 shell 任务继续轮转 on_tick，后续可用 wait 等待。
 * 未找到返回 RS_ARGS_ERR_UNKNOWN；解析失败返回 RS_ARGS_ERR_*；
 * 否则返回处理函数的返回值。
 */
int rs_builtin_dispatch(roshell_t *rs, int argc, char **argv);

/* ============================== 生命周期通知 ============================== */
/*
 * 由 shell 的 pause/resume/kill 等入口调用：查找指定任务当前正在执行的
 * 命令，并调用其 on_pause / on_resume / on_stop 钩子（未实现则忽略）。
 * 钩子在调用者任务上下文中同步执行，不应长时间阻塞。
 * rs_cmd_notify_kill 调用 on_stop 后会清除该任务的命令登记。
 */
int rs_cmd_notify_pause(const struct ros_task *task);
int rs_cmd_notify_resume(const struct ros_task *task);
int rs_cmd_notify_kill(const struct ros_task *task);

/* ============================== 命令模块内部接口 ============================== */
/*
 * 以下接口仅供 roshell 各实现文件使用（平台 / 组件代码不应依赖）：
 * 运行中命令状态机、内建命令表查表、错误输出与会话查询。
 */

/* 格式化错误输出：ERR <code>: <msg> */
void rs_error(int code, const char *fmt, ...);

/* 会话输入任务（未作为会话启动 / rs 为空时返回 NULL） */
struct ros_task *rs_session_task(const roshell_t *rs);

/* 会话启动时登记的版本串，供 ver 命令显示；rs 为空或无版本返回 NULL */
const char *rs_session_version(const roshell_t *rs);

/* 执行一条命令：wait=true 同步执行到结束；
 * wait=false '-' 异步启动，on_start 返回 RS_CMD_RUNNING 后立即返回。 */
int rs_cmd_execute(roshell_t *rs, const rs_cmd_t *def, int argc, char **argv,
                   const rs_args_parsed_t *parsed, void *default_user,
                   bool wait);

/* 解析参数并执行（argv 不含命令名）；wait 语义同上 */
int rs_cmd_exec_node(roshell_t *rs, const rs_cmd_t *def, int argc,
                     char **argv, bool wait);

/* shell 任务每轮调用：推进一步本实例的异步持续命令 */
void rs_cmd_tick_all(roshell_t *rs);

/* 本实例是否存在运行中的异步持续命令（shell 据此决定空闲轮询间隔） */
bool rs_cmd_has_async(roshell_t *rs);

/* 等待本实例的异步持续命令结束；name 为 NULL 表示全部；timeout_ms=0 无限等待 */
int rs_cmd_wait_async(roshell_t *rs, const rs_cmd_t *self, const char *name,
                      uint32_t timeout_ms);

/* 内建命令静态表（命令表初始化时登记到全局 map）；
 * 返回首元素地址并写入条目数，count 可为 NULL。 */
const rs_cmd_t *rs_builtin_table(uint32_t *count);

/* def 是否属于内建静态表 */
bool rs_builtin_is(const rs_cmd_t *def);

/* 按名称查找内建命令（大小写敏感），未命中返回 NULL */
const rs_cmd_t *rs_builtin_find(const char *name);

/* help 内建命令：输出内建命令与 rs_cmd 全局命令表的用法 */
int rs_builtin_help(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user);

#ifdef __cplusplus
}
#endif

#endif /* RS_CMD_H */
