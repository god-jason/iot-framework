#ifndef ROS_SEM_H
#define ROS_SEM_H

/*
 * ros_sem.h - 计数信号量（ros_sem.c）
 *
 * take 在计数为 0 时按 timeout 阻塞；give 直接交接给最高优先级等待者。
 */

#include "ros_types.h"
#include "ros_wait.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ros_sem {
    uint32_t count;                 /* 当前计数值（上限 ROS_SEM_MAX_COUNT） */
    ros_wait_queue_t wait;          /* 等待该信号量的任务 */
    bool initialized;               /* 初始化标志 */
};

int ros_sem_init(ros_sem_t *sem, uint32_t initial_count);
int ros_sem_take(ros_sem_t *sem, uint32_t timeout);
int ros_sem_give(ros_sem_t *sem);

/* 动态创建/销毁：对象由 ros_mem_* 分配，失败返回 NULL */
ros_sem_t *ros_sem_create(uint32_t initial_count);

/* 销毁动态信号量；仍有任务等待时返回 ROS_BUSY，不释放 */
int ros_sem_destroy(ros_sem_t *sem);

#ifdef __cplusplus
}
#endif

#endif /* ROS_SEM_H */
