#ifndef TEST_H
#define TEST_H

/*
 * test.h - 单元测试框架（无外部依赖）
 *
 * 用法：
 *   void ut_xxx(void) {
 *       tf_begin("模块/用例名");
 *       TF_CHECK(cond);
 *       TF_CHECK_INT(actual, expected);
 *       tf_end();
 *   }
 *
 * 约定：
 *   - 每个测试文件导出一个 void ut_xxx(void)；
 *   - 一个 ut_xxx 内可包含多个 tf_begin/tf_end 分组；
 *   - 分组内出现失败时，该组打印 FAIL，但不中断后续执行；
 *   - 计数与失败数在 test_main.c 汇总。
 */

#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int tf_checks;      /* 累计断言数 */
extern int tf_failures;    /* 累计失败数 */

/* 开始一个命名分组 */
void tf_begin(const char *name);

/* 结束分组并打印 PASS/FAIL */
void tf_end(void);

/* 条件为真 */
#define TF_CHECK(cond) do {                                          \
    tf_checks++;                                                     \
    if (!(cond)) {                                                   \
        tf_failures++;                                               \
        printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                \
} while (0)

/* 整数相等 */
#define TF_CHECK_INT(actual, expected) do {                          \
    long _a = (long)(actual);                                        \
    long _e = (long)(expected);                                      \
    tf_checks++;                                                     \
    if (_a != _e) {                                                  \
        tf_failures++;                                               \
        printf("    FAIL %s:%d: %s = %ld, expected %ld\n",           \
               __FILE__, __LINE__, #actual, _a, _e);                 \
    }                                                                \
} while (0)

/* 字符串相等（NULL 视为失败） */
#define TF_CHECK_STR(actual, expected) do {                          \
    const char *_a = (actual);                                       \
    const char *_e = (expected);                                     \
    tf_checks++;                                                     \
    if (_a == NULL || strcmp(_a, _e) != 0) {                         \
        tf_failures++;                                               \
        printf("    FAIL %s:%d: \"%s\" != \"%s\"\n",                 \
               __FILE__, __LINE__, _a ? _a : "(null)", _e);          \
    }                                                                \
} while (0)

/* 浮点近似相等 */
#define TF_CHECK_NEAR(actual, expected, eps) do {                    \
    double _a = (double)(actual);                                    \
    double _e = (double)(expected);                                  \
    double _eps = (double)(eps);                                     \
    tf_checks++;                                                     \
    if (!(_a - _e <= _eps && _e - _a <= _eps)) {                     \
        tf_failures++;                                               \
        printf("    FAIL %s:%d: %s = %f, expected %f (+-%f)\n",      \
               __FILE__, __LINE__, #actual, _a, _e, _eps);           \
    }                                                                \
} while (0)

/* 内存内容相等 */
#define TF_CHECK_MEM(actual, expected, len) do {                     \
    tf_checks++;                                                     \
    if (memcmp((actual), (expected), (len)) != 0) {                  \
        tf_failures++;                                               \
        printf("    FAIL %s:%d: %s 内容不匹配\n",                    \
               __FILE__, __LINE__, #actual);                         \
    }                                                                \
} while (0)

#ifdef __cplusplus
}
#endif

#endif /* TEST_H */
