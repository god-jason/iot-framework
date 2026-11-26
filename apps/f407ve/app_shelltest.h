#ifndef APP_SHELLTEST_H
#define APP_SHELLTEST_H

/*
 * app_shelltest.h - roshell 长命令生命周期上板自检
 *
 * 覆盖：
 *   - '-' 异步启动（立即返回）与 shell 任务 tick 推进；
 *   - wait：等待全部异步长命令、timeout 超时路径；
 *   - sleep 同步长命令；
 *   - pause/resume：暂停期间不消耗 sleep 时间，恢复后只等剩余时间。
 *
 * APP_SHELLTEST_ENABLE=1 时在 app_main 启动阶段同步执行；
 * APP_SHELLTEST_ENABLE=0 时不参与编译。
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APP_SHELLTEST_ENABLE
#define APP_SHELLTEST_ENABLE 1
#endif

/* 同步执行一次自检并打印 PASS/FAIL；重复调用无副作用 */
void app_shelltest_run(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SHELLTEST_H */
