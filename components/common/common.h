#ifndef COMMON_H
#define COMMON_H

/*
 * common.h - 基础组件与标准库统一入口
 *
 * 聚合通用能力：
 *   - 标准库：stdint.h / stddef.h / stdbool.h / string.h / stdlib.h
 *   - 基础组件：log / strings / list / fsm / queue / ringbuf / json
 *
 * src 之外的代码包含 ros.h（或直接包含本文件）即可使用上述接口，
 * 无需再逐个包含基础组件的头文件。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "log.h"      /* 内核日志宏 LOG_D/I/W/E */
#include "strings.h"  /* 通用字符串处理 */
#include "list.h"     /* 侵入式双向链表 */
#include "map.h"      /* 定长字符串键映射表 */
#include "fsm.h"      /* 表驱动状态机 */
#include "queue.h"    /* 定长 FIFO 队列 */
#include "ringbuf.h"  /* 字节环形缓冲 */
#include "json.h"     /* cJSON 宏封装 */

#endif /* COMMON_H */
