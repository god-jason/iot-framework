/*
 * map.c - 字符串键映射表（跳表实现）
 */

#include "map.h"
#include "ros.h"
#include "strings.h"

#define MAP_SEED_INIT 0x9E3779B9U

struct map_node {
    map_item_t       item;   /* key（堆副本）/ value */
    uint8_t          level;  /* 该节点层数 1..MAP_MAX_LEVEL */
    map_node_t      *next[]; /* 柔性数组：level 个前向指针 */
};

/* xorshift32：用于生成随机层数，避免依赖 rand() */
static uint32_t map_rand(map_t *m)
{
    uint32_t x = m->seed;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    m->seed = x;
    return x;
}

static uint8_t map_random_level(map_t *m)
{
    uint8_t level = 1U;

    while (level < MAP_MAX_LEVEL && (map_rand(m) & 1U) != 0U) {
        level++;
    }
    return level;
}

static void map_free_node(map_node_t *node)
{
    if (node != NULL) {
        ros_mem_free((void *)node->item.key);
        ros_mem_free(node);
    }
}

/* 找到 key 的前驱节点，写入 update[]；返回命中的节点或 NULL */
static map_node_t *map_find_update(const map_t *m, const char *key,
                                   map_node_t **update)
{
    map_node_t *x;
    int i;

    x = m->head;
    for (i = (int)MAP_MAX_LEVEL - 1; i >= 0; i--) {
        while (x->next[i] != NULL &&
               strcmp(x->next[i]->item.key, key) < 0) {
            x = x->next[i];
        }
        if (update != NULL) {
            update[i] = x;
        }
    }
    x = x->next[0];
    if (x != NULL && strcmp(x->item.key, key) == 0) {
        return x;
    }
    return NULL;
}

int map_init(map_t *m)
{
    map_node_t *head;

    if (m == NULL) {
        return -1;
    }
    memset(m, 0, sizeof(*m));
    head = (map_node_t *)ros_mem_calloc(
        1U, sizeof(*head) + (size_t)MAP_MAX_LEVEL * sizeof(map_node_t *));
    if (head == NULL) {
        return -1;
    }
    head->level = (uint8_t)MAP_MAX_LEVEL;
    m->head = head;
    m->seed = MAP_SEED_INIT;
    return 0;
}

void map_clear(map_t *m)
{
    map_node_t *x;
    map_node_t *next;

    if (m == NULL || m->head == NULL) {
        return;
    }
    x = m->head->next[0];
    while (x != NULL) {
        next = x->next[0];
        map_free_node(x);
        x = next;
    }
    memset(m->head->next, 0,
           (size_t)MAP_MAX_LEVEL * sizeof(map_node_t *));
    m->count = 0U;
}

void map_deinit(map_t *m)
{
    if (m == NULL) {
        return;
    }
    map_clear(m);
    ros_mem_free(m->head);
    m->head = NULL;
    m->count = 0U;
}

bool map_get(const map_t *m, const char *key, void **out)
{
    map_node_t *node;

    if (m == NULL || m->head == NULL || key == NULL || key[0] == '\0') {
        return false;
    }
    node = map_find_update(m, key, NULL);
    if (node == NULL) {
        return false;
    }
    if (out != NULL) {
        *out = node->item.value;
    }
    return true;
}

bool map_contains(const map_t *m, const char *key)
{
    return map_get(m, key, NULL);
}

void *map_get_ptr(const map_t *m, const char *key)
{
    void *value = NULL;

    if (!map_get(m, key, &value)) {
        return NULL;
    }
    return value;
}

bool map_put(map_t *m, const char *key, void *value)
{
    map_node_t *update[MAP_MAX_LEVEL];
    map_node_t *node;
    char *key_copy;
    uint8_t level;
    uint8_t i;

    if (m == NULL || m->head == NULL || key == NULL || key[0] == '\0') {
        return false;
    }
    node = map_find_update(m, key, update);
    if (node != NULL) {
        node->item.value = value;   /* 已存在：仅更新值 */
        return true;
    }

    level = map_random_level(m);
    key_copy = str_dup(key);
    if (key_copy == NULL) {
        return false;
    }
    node = (map_node_t *)ros_mem_calloc(
        1U, sizeof(*node) + (size_t)level * sizeof(map_node_t *));
    if (node == NULL) {
        ros_mem_free(key_copy);
        return false;
    }
    node->item.key = key_copy;
    node->item.value = value;
    node->level = level;
    for (i = 0U; i < level; i++) {
        node->next[i] = update[i]->next[i];
        update[i]->next[i] = node;
    }
    m->count++;
    return true;
}

bool map_remove(map_t *m, const char *key)
{
    map_node_t *update[MAP_MAX_LEVEL];
    map_node_t *node;
    uint8_t i;

    if (m == NULL || m->head == NULL || key == NULL || key[0] == '\0') {
        return false;
    }
    node = map_find_update(m, key, update);
    if (node == NULL) {
        return false;
    }
    for (i = 0U; i < node->level; i++) {
        update[i]->next[i] = node->next[i];
    }
    m->count--;
    map_free_node(node);
    return true;
}

const map_item_t *map_at(const map_t *m, uint32_t index)
{
    map_node_t *x;

    if (m == NULL || m->head == NULL) {
        return NULL;
    }
    x = m->head->next[0];
    while (x != NULL && index > 0U) {
        x = x->next[0];
        index--;
    }
    return (x != NULL) ? &x->item : NULL;
}
