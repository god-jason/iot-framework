#ifndef APP_H
#define APP_H

/*
 * app.h - 应用入口约定
 *
 * app_main() 是独立任务入口：板级 main() 完成板级初始化（时钟 / 串口 /
 * ros_init）后创建该任务，再启动调度器。apps/<name>/app_main.c 提供强定义；
 * 未链接任何应用时使用 src/app/app.c 的弱定义（打印接入提示后等待）。
 *
 * 约定：
 *   - 任务体内不要调用 ros_sched_start()（调度器由板级 main 启动）；
 *   - 任务体返回即任务结束，其它任务继续运行；
 *   - 需要结束整个应用时调用 ros_stop()，并用 app_set_exit_code() 留下退出码，
 *     板级 main 在 ros_sched_start() 返回后读取 app_get_exit_code()。
 */

#ifdef __cplusplus
extern "C" {
#endif

void app_main(void *arg);

/* 应用退出码：任务内设置，板级 main 读取（默认 0） */
void app_set_exit_code(int code);
int app_get_exit_code(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */
