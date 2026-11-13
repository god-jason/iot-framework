#ifndef ROS_IO_H
#define ROS_IO_H

/*
 * ros_io.h - 标准输入输出抽象（ros_io.c）
 *
 * 把任务的输入输出与具体硬件解耦：UART、虚拟串口、网络连接等只需实现
 * read / write / close 三个回调，就能作为任务的 stdio。
 *
 * 回调约定：
 *   read  : >0 实际读到的字节数；0 暂时无数据（或对端暂无数据）；
 *           <0 输入关闭 / 错误（任务应视为 EOF）
 *   write : 返回写入字节数；<0 失败
 *   close : 释放底层资源；可为 NULL，表示无需关闭
 *
 * 默认 stdio 初始为空（null io），由平台层在 ros_init() 中初始化：
 *   PC    → 标准输入输出（pc_io.c）
 *   STM32 → UART1（stm32_io.c）
 * 任务创建时继承创建者的 stdio，因此脚本 JOB 天然共享父任务的 stdio；
 * 也可以在创建任务时显式指定（ros_task_create_dynamic_io）。
 *
 * 另提供 stdio 风格接口（ros_putchar / ros_puts / ros_printf /
 * ros_getchar / ros_gets / ros_scanf），读写都落到当前任务 stdio，
 * 任务内可直接替换标准库的同名调用。
 */

#include "ros_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 格式化输出 / 整行输入的缓冲大小，可按需裁剪 */
#ifndef ROS_IO_PRINT_MAX
#define ROS_IO_PRINT_MAX        192U
#endif
#ifndef ROS_IO_LINE_MAX
#define ROS_IO_LINE_MAX         128U
#endif

typedef struct ros_io {
    int (*read)(struct ros_io *io, void *buf, uint32_t len);
    int (*write)(struct ros_io *io, const void *buf, uint32_t len);
    int (*close)(struct ros_io *io);
    void *user;                 /* 实现私有上下文，可为 NULL */
} ros_io_t;

/*
 * 平台默认 stdio 工厂：PC 为 stdin/stdout，STM32 为 UART1。
 * 由平台层实现，供 ros_io_platform_init() 注册为默认 stdio。
 */
ros_io_t *ros_io_platform_default(void);

/*
 * 平台 stdio 初始化：由 ros_init() 调用（弱符号）。
 * 平台层在此把默认 stdio 设为 PC 标准输入输出或 UART1；
 * 未实现时默认 stdio 保持为空（null io：丢弃输出、无输入）。
 */
void ros_io_platform_init(void);

/* 注册 / 查询全局默认 stdio（io 为 NULL 表示恢复为空） */
void ros_io_set_default(ros_io_t *io);
ros_io_t *ros_io_default(void);

/* 当前任务绑定的 stdio；未绑定或无任务上下文时返回默认 stdio */
ros_io_t *ros_stdio(void);

/*
 * 绑定当前任务的 stdio（io 为 NULL 表示恢复默认）。
 * 无任务上下文（调度器未启动 / 中断）时返回 -1。
 */
int ros_stdio_set(ros_io_t *io);

/* 便捷读写：io 为 NULL 时使用 ros_stdio()。返回字节数，<0 失败 */
int ros_io_read(ros_io_t *io, void *buf, uint32_t len);
int ros_io_write(ros_io_t *io, const void *buf, uint32_t len);
int ros_io_puts(ros_io_t *io, const char *s);
int ros_io_close(ros_io_t *io);

/* 丢弃输入输出（/dev/null）：write 返回 len，read 返回 0，close 返回 0 */
ros_io_t *ros_io_null(void);

/* ============================== stdio 风格接口 ============================== */
/*
 * 与标准 stdio 对应，读写都使用 ros_stdio()（当前任务 stdio，未绑定则默认）。
 * 成功返回非负数，失败 / 输入结束返回 -1。
 */

int ros_putchar(int c);                     /* 输出 1 字节 */
int ros_puts(const char *s);                /* 输出字符串并追加换行 */
int ros_printf(const char *fmt, ...);       /* 格式化输出 */
int ros_getchar(void);                      /* 读 1 字节，无数据时等待 */
int ros_gets(char *buf, uint32_t cap);      /* 读一行（去掉 CR/LF），返回长度 */
int ros_scanf(const char *fmt, ...);        /* 支持 %d %u %x %s %c %% */

#ifdef __cplusplus
}
#endif

#endif /* ROS_IO_H */
