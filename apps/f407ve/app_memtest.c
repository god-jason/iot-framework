/*
 * app_memtest.c - F407 堆内存泄漏自检（启动阶段同步执行）
 *
 * 在压测/文件系统启动前，由 app 任务同步执行一批“分配 -> 使用 -> 释放”
 * 循环，通过 ros_mem 的记账判断是否存在泄漏：
 *   1) 本任务 mem_used 在循环前后必须完全相等（主判据，不受其它任务影响）；
 *   2) 全局堆 used/free 以 MT_TOL_BYTES 容差比较；
 *   3) untracked_bytes 不变。
 *
 * 覆盖路径：
 *   - var/var_json：对象 + 数组 + 标量树，序列化/反序列化后递归释放；
 *   - vec / map：扩容、插入、删除、释放；
 *   - str_dup / ros_mem_free；
 *   - 裸 ros_mem_malloc/free（多种尺寸）；
 *   - 动态任务创建 -> 退出 -> ros_task_destroy（栈与 TCB 回收）。
 *
 * 结果通过串口打印 [I][mt] ... PASS/FAIL。
 */

#include "app_memtest.h"

#include "ros.h"
#include "log.h"
#include "strings.h"
#include "var.h"
#include "var_json.h"

#include <string.h>

/* 循环次数：变量结构 2000 轮，动态任务 20 轮 */
#define MT_ROUNDS          2000U
#define MT_WARM            20U
#define MT_TASK_CYCLES     20U
#define MT_TOL_BYTES       256U

static uint32_t s_err;      /* 逻辑校验失败 */
static uint32_t s_oom;      /* 意外分配失败 */

/* ============================== 变量结构 ============================== */

static var_t *mt_var_new(void)
{
    return (var_t *)ros_mem_calloc(1U, sizeof(var_t));
}

/*
 * 一轮：构建
 *   { "id":7, "name":"sensor", "ok":true, "arr":[1, true, "x"] }
 * 序列化 + 标量反序列化后，var_free 递归释放整棵树。
 * 注意：字符串为借用语义，不做释放。
 */
static void mt_cycle_var(void)
{
    var_t root;
    var_t item;
    var_t parsed;
    map_t *m = NULL;
    vec_t *arr = NULL;
    var_t *v = NULL;
    bool m_inited = false;
    bool arr_inited = false;
    char sbuf[16];
    char *text;

    var_init(&root);

    m = (map_t *)ros_mem_calloc(1U, sizeof(*m));
    if (m == NULL || map_init(m) != 0) {
        s_oom++;
        goto cleanup;
    }
    m_inited = true;

    /* id */
    v = mt_var_new();
    if (v == NULL) {
        s_oom++;
        goto cleanup;
    }
    var_set_int(v, 7);
    if (!map_put(m, "id", v)) {
        ros_mem_free(v);
        v = NULL;
        s_err++;
        goto cleanup;
    }
    v = NULL;

    /* name（借用常量字符串） */
    v = mt_var_new();
    if (v == NULL) {
        s_oom++;
        goto cleanup;
    }
    var_set_string(v, "sensor");
    if (!map_put(m, "name", v)) {
        ros_mem_free(v);
        v = NULL;
        s_err++;
        goto cleanup;
    }
    v = NULL;

    /* ok */
    v = mt_var_new();
    if (v == NULL) {
        s_oom++;
        goto cleanup;
    }
    var_set_bool(v, true);
    if (!map_put(m, "ok", v)) {
        ros_mem_free(v);
        v = NULL;
        s_err++;
        goto cleanup;
    }
    v = NULL;

    /* arr：[1, true, "x"] */
    arr = (vec_t *)ros_mem_calloc(1U, sizeof(*arr));
    if (arr == NULL || vec_init(arr, (uint32_t)sizeof(var_t), 4U) != 0) {
        s_oom++;
        goto cleanup;
    }
    arr_inited = true;
    var_set_int(&item, 1);
    if (vec_push(arr, &item) != 0) {
        s_err++;
        goto cleanup;
    }
    var_set_bool(&item, true);
    if (vec_push(arr, &item) != 0) {
        s_err++;
        goto cleanup;
    }
    var_set_string(&item, "x");
    if (vec_push(arr, &item) != 0) {
        s_err++;
        goto cleanup;
    }

    v = mt_var_new();
    if (v == NULL) {
        s_oom++;
        goto cleanup;
    }
    var_set_array(v, arr);
    if (!map_put(m, "arr", v)) {
        var_free(v);                    /* 释放 vec 与元素 */
        ros_mem_free(v);
        v = NULL;
        arr_inited = false;
        s_err++;
        goto cleanup;
    }
    v = NULL;
    arr_inited = false;                 /* 所有权转给 var 树 */

    var_set_object(&root, m);
    m_inited = false;                   /* 所有权转给 root */

    /* 序列化 / 反序列化 */
    text = var_to_json(&root);
    if (text == NULL) {
        s_err++;
    }
    var_free_json(text);

    if (!var_from_json("-12345", &parsed, sbuf, sizeof(sbuf)) ||
        parsed.value.i != -12345) {
        s_err++;
    }
    if (!var_from_json("\"s\"", &parsed, sbuf, sizeof(sbuf)) ||
        var_get_string(&parsed) == NULL) {
        s_err++;
    }

cleanup:
    if (v != NULL) {
        ros_mem_free(v);
    }
    if (arr_inited && arr != NULL) {
        vec_deinit(arr);
        ros_mem_free(arr);
    }
    if (m_inited && m != NULL) {
        map_deinit(m);
        ros_mem_free(m);
    }
    var_free(&root);                    /* VAR_NONE 时为空操作 */
}

/* ============================== 容器 ============================== */

static void mt_cycle_container(void)
{
    vec_t *a = (vec_t *)ros_mem_calloc(1U, sizeof(*a));
    map_t *m = (map_t *)ros_mem_calloc(1U, sizeof(*m));
    bool a_inited = false;
    bool m_inited = false;
    uint32_t i;
    char key[8];

    if (a == NULL || m == NULL) {
        s_oom++;
        goto cleanup;
    }
    if (vec_init(a, (uint32_t)sizeof(uint32_t), 8U) != 0) {
        s_err++;
        goto cleanup;
    }
    a_inited = true;
    for (i = 0U; i < 8U; i++) {
        if (vec_push(a, &i) != 0) {
            s_err++;
        }
    }
    if (map_init(m) != 0) {
        s_err++;
        goto cleanup;
    }
    m_inited = true;
    for (i = 0U; i < 8U; i++) {
        (void)str_printf(key, (uint32_t)sizeof(key), "k%u", (unsigned)i);
        if (!map_put(m, key, (void *)(uintptr_t)(i + 1U))) {
            s_err++;
        }
    }
    (void)map_get_ptr(m, "k3");
    (void)map_remove(m, "k5");

cleanup:
    if (m_inited) {
        map_deinit(m);
    }
    if (a_inited) {
        vec_deinit(a);
    }
    ros_mem_free(m);
    ros_mem_free(a);
}

/* ============================== 其它分配 ============================== */

static void mt_cycle_raw(void)
{
    void *p[6];
    uint32_t i;

    for (i = 0U; i < 6U; i++) {
        p[i] = ros_mem_malloc((size_t)(17U * (i + 1U)));
        if (p[i] == NULL) {
            s_oom++;
        }
    }
    for (i = 0U; i < 6U; i++) {
        ros_mem_free(p[i]);
    }
}

static void mt_cycle_str(void)
{
    char *s = str_dup("leak-test-0123456789");

    if (s == NULL) {
        s_oom++;
        return;
    }
    if (strlen(s) != 20U) {
        s_err++;
    }
    ros_mem_free(s);
}

/* ============================== 动态任务 ============================== */

static void mt_task_entry(void *arg)
{
    (void)arg;                          /* 立即返回 -> 任务退出 */
}

static void mt_cycle_task(void)
{
    ros_task_t *t = ros_task_create_dynamic("mt_tmp", 2U, mt_task_entry,
                                            NULL, 512U);
    uint32_t guard = 0U;

    if (t == NULL) {
        s_oom++;
        return;
    }
    /* 等它跑完退出（状态变 TERMINATED），最多等 200ms */
    while (t->state != ROS_TASK_TERMINATED && guard < 200U) {
        ros_sleep_ms(1U);
        guard++;
    }
    if (t->state != ROS_TASK_TERMINATED) {
        s_err++;
        (void)ros_task_destroy(t);      /* 兜底回收 */
        return;
    }
    if (ros_task_destroy(t) != ROS_OK) {
        s_err++;
    }
}

/* ============================== 测试入口 ============================== */

static void mt_report(const ros_mem_info_t *a, const ros_mem_info_t *b,
                      uint32_t task_a, uint32_t task_b)
{
    LOG_I("mt", "memtest %s: err=%u oom=%u",
          (s_err == 0U) ? "PASS" : "FAIL",
          (unsigned)s_err, (unsigned)s_oom);
    LOG_I("mt", "  task mem : %u -> %u B",
          (unsigned)task_a, (unsigned)task_b);
    LOG_I("mt", "  heap used: %u -> %u B, free: %u -> %u B",
          (unsigned)a->used_bytes, (unsigned)b->used_bytes,
          (unsigned)a->free_bytes, (unsigned)b->free_bytes);
    LOG_I("mt", "  max block: %u -> %u B, untracked: %u -> %u B",
          (unsigned)a->max_free_bytes, (unsigned)b->max_free_bytes,
          (unsigned)a->untracked_bytes, (unsigned)b->untracked_bytes);
}

void app_memtest_run(void)
{
    static uint8_t done;
    ros_mem_info_t a;
    ros_mem_info_t b;
    uint32_t task_a;
    uint32_t task_b;
    uint32_t i;

    if (done != 0U) {
        return;
    }
    done = 1U;

    LOG_I("mt", "memtest start: rounds=%u, task-cycles=%u",
          (unsigned)MT_ROUNDS, (unsigned)MT_TASK_CYCLES);

    /* 预热：懒初始化/缓存先触发，之后才取基线 */
    for (i = 0U; i < MT_WARM; i++) {
        mt_cycle_var();
        mt_cycle_container();
        mt_cycle_raw();
        mt_cycle_str();
    }
    (void)ros_mem_get_info(&a);
    task_a = ros_current_task->mem_used;

    /* 正式循环 */
    for (i = 0U; i < MT_ROUNDS; i++) {
        mt_cycle_var();
        mt_cycle_container();
        mt_cycle_raw();
        mt_cycle_str();
    }
    for (i = 0U; i < MT_TASK_CYCLES; i++) {
        mt_cycle_task();
    }

    (void)ros_mem_get_info(&b);
    task_b = ros_current_task->mem_used;

    /* 主判据：本任务持有量必须完全回到基线 */
    if (task_b != task_a) {
        s_err++;
        LOG_E("mt", "TASK LEAK: mem_used %u -> %u (+%d B)",
              (unsigned)task_a, (unsigned)task_b,
              (int)task_b - (int)task_a);
    }
    /* 辅判据：全局堆（容差应对少量并发分配） */
    if (b.used_bytes > a.used_bytes + MT_TOL_BYTES) {
        s_err++;
        LOG_E("mt", "HEAP LEAK: used %u -> %u (+%d B)",
              (unsigned)a.used_bytes, (unsigned)b.used_bytes,
              (int)b.used_bytes - (int)a.used_bytes);
    }
    if (b.untracked_bytes != a.untracked_bytes) {
        s_err++;
        LOG_E("mt", "UNTRACKED changed: %u -> %u B",
              (unsigned)a.untracked_bytes, (unsigned)b.untracked_bytes);
    }

    mt_report(&a, &b, task_a, task_b);
}
