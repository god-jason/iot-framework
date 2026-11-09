#ifndef MOTION_PARSER_H
#define MOTION_PARSER_H

/*
 * motion_parser.h —— MotionScript 单行语法解析接口。
 *
 * 职责：把数据源读到的一行文本解析为 motion_instruction_t；
 *       同时对外暴露"值 token"解析能力，供表达式求值等模块复用。
 *
 * 数据流：motion_source 提供文本行 -> motion_parser_parse() 去掉注释、
 *         识别命令字并拆出参数 -> 执行层按 command 分发。
 *
 * 边界约束：解析全程只用调用方提供的结构体与函数栈上的定长数组，
 *           不做动态分配；单行长度上限为 MOTION_MAX_LINE，
 *           参数个数上限为 MOTION_MAX_PARAMS。
 */

#include "motion_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 解析一整行文本。
 * 参数：line 以 '\0' 结尾的源行（可含 ';' 或 '//' 注释）；
 *       instruction 输出结构，函数入口会先整体清零。
 * 返回：MOTION_PARSE_OK 成功；EMPTY 空行；INVALID 语法错误；
 *       OVERFLOW 行过长或参数超限；UNSUPPORTED 未知指令。
 * 说明：instruction 为 NULL 或 line 为 NULL 时返回 INVALID。 */
motion_parse_result_e motion_parser_parse(const char *line,
                                          motion_instruction_t *instruction);

/* 解析一个值（整数/浮点/布尔/字符串/$变量，可带单位）。
 * 参数：cursor 输入输出游标，成功时推进到 token 之后；
 *       value 输出数值，解析前会被清零。
 * 返回：0 成功；-1 参数非法或 token 无法识别。 */
int motion_parser_value_token(const char **cursor, motion_value_t *value);

/* 解析独立的单个值文本（要求整串除空白/逗号外无多余内容）。
 * 参数：text 待解析文本；value 输出数值。
 * 返回：0 成功；-1 文本为空、非法或存在尾随多余内容。 */
int motion_parser_parse_value(const char *text, motion_value_t *value);

/* 把解析结果码转换为静态英文描述字符串（仅用于日志）。
 * 参数：result 解析结果码。
 * 返回：指向常量字符串的指针，未知码返回 "unknown parse result"。 */
const char *motion_parse_result_message(motion_parse_result_e result);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_PARSER_H */
