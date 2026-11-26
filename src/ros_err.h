#ifndef ROS_ERR_H
#define ROS_ERR_H

/*
 * ros_err.h - 内核统一状态码
 *
 * 所有返回 int 的内核 API 共用这里的值：
 *   - ROS_OK / 正数表示成功（部分 API 用正数返回字节数、条数等）；
 *   - 负数表示错误，调用方可直接与常量比较。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 状态值统一以 int 返回，枚举仅用于定义常量名 */
enum {
    ROS_OK = 0,            /* 成功 */
    ROS_ERROR = -1,        /* 未分类错误 */
    ROS_TIMEOUT = -2,      /* 等待超时 */
    ROS_INVALID_PARAM = -3,/* 参数非法（空指针/越界） */
    ROS_NO_MEMORY = -4,    /* 资源不足（任务/等待队列已满等） */
    ROS_NOT_READY = -5,    /* 未初始化或前置条件不满足 */
    ROS_BUSY = -6,         /* 资源被占用 */
    ROS_NOT_OWNER = -7,    /* 当前任务不是该资源的持有者 */
    ROS_OVERFLOW = -8,     /* 计数溢出（信号量超过上限等） */
    ROS_FORBIDDEN_IN_ISR = -9 /* 中断上下文中调用了可能阻塞的 API */
};

#ifdef __cplusplus
}
#endif

#endif /* ROS_ERR_H */
