#ifndef TEST_H
#define TEST_H

/*
 * 单元测试宏与断言框架（无外部依赖）。
 * 每个测试文件导出 ut_xxx()，内部用 tf_begin()/tf_end() 标记子用例。
 */

#include <stdio.h>
#include <string.h>

extern int tf_checks;
extern int tf_failures;

void tf_begin(const char *name);
void tf_end(void);

#define TF_CHECK(cond) do { \
    tf_checks++; \
    if (!(cond)) { \
        tf_failures++; \
        printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define TF_CHECK_INT(actual, expected) do { \
    long _a = (long)(actual); \
    long _e = (long)(expected); \
    tf_checks++; \
    if (_a != _e) { \
        tf_failures++; \
        printf("    FAIL %s:%d: %s = %ld, expected %ld\n", \
               __FILE__, __LINE__, #actual, _a, _e); \
    } \
} while (0)

#define TF_CHECK_STR(actual, expected) do { \
    const char *_a = (actual); \
    const char *_e = (expected); \
    tf_checks++; \
    if (_a == NULL || strcmp(_a, _e) != 0) { \
        tf_failures++; \
        printf("    FAIL %s:%d: \"%s\" != \"%s\"\n", \
               __FILE__, __LINE__, _a ? _a : "(null)", _e); \
    } \
} while (0)

#define TF_CHECK_NEAR(actual, expected, eps) do { \
    double _a = (double)(actual); \
    double _e = (double)(expected); \
    double _eps = (double)(eps); \
    tf_checks++; \
    if (!(_a - _e <= _eps && _e - _a <= _eps)) { \
        tf_failures++; \
        printf("    FAIL %s:%d: %s = %f, expected %f (+-%f)\n", \
               __FILE__, __LINE__, #actual, _a, _e, _eps); \
    } \
} while (0)

#define TF_CHECK_MEM(actual, expected, len) do { \
    tf_checks++; \
    if (memcmp((actual), (expected), (len)) != 0) { \
        tf_failures++; \
        printf("    FAIL %s:%d: %s 内容不匹配\n", \
               __FILE__, __LINE__, #actual); \
    } \
} while (0)

#endif /* TEST_H */
