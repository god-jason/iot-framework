/*
 * console.c - 控制台与命令行 Shell 实现
 *
 * 控制台绑定一个 UART 句柄（PC 平台为 UART1，已映射到标准输入输出；
 * STM32 通常为 UART1）。
 *
 * 模块职责：
 *   1) 控制台：按行输出（自动追加 CRLF），并泵取 UART 接收数据喂给 Shell；
 *   2) Shell：维护命令表，按空格/制表符切分 argv 并分发执行，
 *      同时支持按行喂入字节流（'\n' 触发一条命令）。
 *
 * 缓冲与边界约束（容量为编译期固定值，内存由 iot_mem 运行期分配）：
 *   - s_cmds：命令表，最多 SHELL_MAX_CMD（16）条，满则注册失败。
 *   - s_line：单行命令缓冲，最多 SHELL_LINE_MAX（128）字节（含 '\0'）。
 *   - shell_execute() 内部另有同尺寸栈缓冲与 16 个 argv 槽位。
 *   - 行缓冲溢出时直接清空当前行（丢弃该行），不会截断执行。
 *   - 控制台为单实例（静态单例），不支持多路 UART 同时接入。
 */

#include "console.h"
#include "log.h"
#include "hal_mem.h"

#include <string.h>

/* 控制台状态：绑定一个 UART 句柄（PC 平台为 UART1，STM32 通常为 UART1） */
static bool s_ready = false;            /* console_init 是否已成功 */
static hal_uart_t *s_uart = NULL;
static uint8_t *s_rx = NULL;
static uint32_t s_rx_size = 0;

/*
 * 初始化控制台。
 * 参数：config - 控制台配置；uart 必须非 NULL。
 *                rx_buffer/rx_buffer_size 用于接收泵取，若为 NULL 或 0，
 *                则 console_process() 只查询计数而不消费数据。
 * 返回：0 成功（Shell 初始化结果）；-1 参数非法
 * 注意：重复调用会用新配置覆盖旧配置，不释放任何资源。
 */
int console_init(const console_config_t *config)
{
    int rc;

    if (config == NULL || config->uart == NULL) {
        return -1;
    }

    s_uart = config->uart;
    s_rx = config->rx_buffer;
    s_rx_size = config->rx_buffer_size;

    rc = shell_init();
    if (rc == 0) {
        s_ready = true;
    }
    return rc;
}

/*
 * 启动控制台：打印一行就绪提示。
 * 参数：无
 * 返回：HAL_OK（平台错误码）；无副作用之外的状态变更
 */
int console_start(void)
{
    console_puts("iot-os console ready");   /* 启动提示：控制台就绪 */
    return HAL_OK;
}

/*
 * 直接输出一行文本（绕过 logger）。
 * 参数：s - 以 '\0' 结尾的字符串，不可为 NULL
 * 返回：HAL_OK 成功；-1 尚未初始化或参数非法
 * 注意：通过绑定的 UART 发送正文与 "\r\n"（PC 平台的 UART1 已映射到 stdout）。
 */
int console_puts(const char *s)
{
    if (!s_ready || s_uart == NULL || s == NULL) {
        return -1;
    }
    /* 输出一行 + CRLF */
    hal_uart_transmit(s_uart, (const uint8_t *)s, strlen(s), 100);
    hal_uart_transmit(s_uart, (const uint8_t *)"\r\n", 2, 100);
    return HAL_OK;
}

/*
 * 主循环调用：泵取 UART 接收数据并喂给 Shell。
 * 返回：HAL_OK 正常处理；-1 控制台未初始化
 * 注意：循环直到接收计数为 0、接收缓冲不可用或底层读失败；
 *       每次最多读取 s_rx_size 字节。
 */
int console_process(void)
{
    uint32_t n;

    if (!s_ready || s_uart == NULL) {
        return -1;
    }

    n = (uint32_t)hal_uart_get_rx_count(s_uart);
    while (n > 0 && s_rx != NULL && s_rx_size > 0) {
        /* 单次读取量不超过接收缓冲容量 */
        uint32_t chunk = (n > s_rx_size) ? s_rx_size : n;
        int r = hal_uart_receive(s_uart, s_rx, chunk, 100);
        if (r != HAL_OK) {
            break;   /* 底层读失败，保留剩余数据待下次处理 */
        }
        shell_input((const char *)s_rx, chunk);
        n = (uint32_t)hal_uart_get_rx_count(s_uart);   /* 可能有新数据到达 */
    }
    return HAL_OK;
}

/* ==================== Shell ==================== */
/* 命令表容量：同时可注册的命令条数 */
#define SHELL_MAX_CMD    16
/* 单行命令最大长度（含结尾 '\0'），超长行整体丢弃 */
#define SHELL_LINE_MAX   128
/* 单行最多切分出的参数个数（含命令名） */
#define SHELL_ARGC_MAX   16

/* 命令表内部条目：命令名、描述与回调 */
typedef struct {
    const char *name;
    const char *description;
    shell_command_fn callback;
} shell_cmd_t;

/* 命令表与行输入缓冲：callback == NULL 表示该槽位空闲；由 iot_mem 分配 */
static shell_cmd_t *s_cmds = NULL;
static char *s_line = NULL;
static uint32_t s_line_len;

/* 确保命令表与行缓冲已分配；分配失败返回 false（再次调用会重试） */
static bool shell_ensure(void)
{
    if (s_cmds == NULL) {
        s_cmds = (shell_cmd_t *)hal_calloc(SHELL_MAX_CMD,
                                            sizeof(shell_cmd_t));
    }
    if (s_line == NULL) {
        s_line = (char *)hal_calloc(SHELL_LINE_MAX, sizeof(char));
    }
    return (s_cmds != NULL && s_line != NULL);
}

/*
 * 初始化 Shell：重新分配命令表与行缓冲并清零。
 * 参数：无
 * 返回：0 成功；-1 内存分配失败
 * 注意：重复调用会先释放旧内存再重新分配；callback 全为 NULL 表示空闲。
 */
int shell_init(void)
{
    hal_free(s_cmds);
    hal_free(s_line);
    s_cmds = (shell_cmd_t *)hal_calloc(SHELL_MAX_CMD, sizeof(shell_cmd_t));
    s_line = (char *)hal_calloc(SHELL_LINE_MAX, sizeof(char));
    s_line_len = 0;
    if (s_cmds == NULL || s_line == NULL) {
        hal_free(s_cmds);
        hal_free(s_line);
        s_cmds = NULL;
        s_line = NULL;
        return -1;
    }
    return 0;
}

/*
 * 注册一条 Shell 命令。
 * 参数：command - 命令描述（name、callback 均不可为 NULL）
 * 返回：0 成功；-1 参数非法或命令表已满
 * 注意：只保存指针，不拷贝字符串，命令名必须是静态存储或长期有效；
 *       不检查同名命令，重复注册会占用新槽位。
 */
int shell_register(const shell_command_t *command)
{
    int i;
    if (command == NULL || command->name == NULL || command->callback == NULL) {
        return -1;
    }
    if (!shell_ensure()) {
        return -1;   /* 内存分配失败 */
    }
    for (i = 0; i < SHELL_MAX_CMD; i++) {
        if (s_cmds[i].callback == NULL) {
            s_cmds[i].name = command->name;
            s_cmds[i].description = command->description;
            s_cmds[i].callback = command->callback;
            return 0;
        }
    }
    return -1;   /* 命令表已满 */
}

/*
 * 按名称注销 Shell 命令。
 * 参数：name - 命令名，不可为 NULL
 * 返回：0 成功；-1 参数非法或未找到该命令
 * 注意：只清空 name/callback，description 残留但不影响查找。
 */
int shell_unregister(const char *name)
{
    int i;
    if (name == NULL) {
        return -1;
    }
    if (!shell_ensure()) {
        return -1;   /* 内存分配失败 */
    }
    for (i = 0; i < SHELL_MAX_CMD; i++) {
        if (s_cmds[i].callback != NULL && strcmp(s_cmds[i].name, name) == 0) {
            s_cmds[i].name = NULL;
            s_cmds[i].callback = NULL;
            return 0;
        }
    }
    return -1;
}

/*
 * 执行一行命令（同步调用命令回调）。
 * 参数：command - 命令文本（不可为 NULL），以空格或制表符分隔参数
 * 返回：命令回调的返回值；-1 参数非法、空命令或未找到命令
 * 注意：先把命令拷贝到 128 字节栈缓冲再原地切分（会修改副本），
 *       超出 127 字节的部分被截断；参数最多 16 个，多余参数被忽略。
 */
int shell_execute(const char *command)
{
    char buf[SHELL_LINE_MAX];
    char *argv[SHELL_ARGC_MAX];
    int argc = 0;
    char *p;
    int i;

    if (command == NULL) {
        return -1;
    }
    if (!shell_ensure()) {
        return -1;   /* 内存分配失败 */
    }

    /* 拷贝到可变缓冲并切分（保留 1 字节给 '\0'） */
    strncpy(buf, command, sizeof(buf) - 1u);
    buf[sizeof(buf) - 1u] = '\0';
    p = buf;
    while (*p != '\0' && argc < SHELL_ARGC_MAX) {
        /* 跳过前导空白 */
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = p;   /* 记录参数起始位置 */
        /* 找到参数结尾 */
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';    /* 就地截断，形成独立字符串 */
        }
    }

    if (argc == 0) {
        return -1;   /* 空行或全空白 */
    }

    for (i = 0; i < SHELL_MAX_CMD; i++) {
        if (s_cmds[i].callback != NULL && strcmp(s_cmds[i].name, argv[0]) == 0) {
            return s_cmds[i].callback(argc, argv);
        }
    }
    return -1;   /* 未注册的命令 */
}

/*
 * 向 Shell 喂入字节流，按行触发命令执行。
 * 参数：data - 字节流；len - 字节数
 * 返回：0 处理完成；-1 参数非法
 * 注意：'\r' 被忽略（兼容 CRLF）；'\n' 表示一行结束并立即执行；
 *       退格（'\b'/127）删除行缓冲最后一个字符；
 *       行缓冲达到 127 字节后若再来新字符，则清空当前行（丢弃整行）；
 *       命令回调的返回值被忽略。
 */
int shell_input(const char *data, uint32_t len)
{
    uint32_t i;

    if (data == NULL || len == 0) {
        return -1;
    }
    if (!shell_ensure()) {
        return -1;   /* 内存分配失败 */
    }
    for (i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\r') {
            continue;   /* CRLF 中的 '\r' 不参与组行 */
        }
        if (c == '\n') {
            s_line[s_line_len] = '\0';
            s_line_len = 0;
            (void)shell_execute(s_line);
        } else if (c == '\b' || c == 127) {
            if (s_line_len > 0U) {
                s_line_len--;   /* 退格：删除行缓冲最后一个字符 */
            }
        } else if (s_line_len < SHELL_LINE_MAX - 1) {
            s_line[s_line_len++] = c;
        } else {
            s_line_len = 0;   /* 溢出丢弃 */
        }
    }
    return 0;
}

/*
 * Shell 周期处理钩子（预留）。
 * 参数：无
 * 返回：0
 * 注意：当前无内部状态需要轮询，命令执行由 shell_input() 直接触发。
 */
int shell_process(void)
{
    return 0;
}
