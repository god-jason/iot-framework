#ifndef APP_STRESS_H
#define APP_STRESS_H

/*
 * app_stress.h - 内核/调度器稳定性压测（12 任务、深递归、整数重算、同步原语）
 *
 * APP_STRESS_ENABLE = 1：编译并开机自动启动；
 * APP_STRESS_ENABLE = 0：完全不参与编译（app_main 中的调用被预处理掉）。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 默认关闭：压测任务会持续占用 CPU 并刷日志，不能进发布固件；
 * 需要时由构建参数（-DAPP_STRESS_ENABLE=1）或专用 stress 目标打开。 */
#ifndef APP_STRESS_ENABLE
#define APP_STRESS_ENABLE 0
#endif

/* 创建 12 个压测任务并开始运行；重复调用无副作用 */
void app_stress_start(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_STRESS_H */
