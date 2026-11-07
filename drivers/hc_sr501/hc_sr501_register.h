#ifndef HC_SR501_REGISTER_H
#define HC_SR501_REGISTER_H

/*
 * HC-SR501 人体红外（PIR）模块电气/时序参数。
 *
 * 模块数字输出：检测到移动时 OUT 拉高，Tx（延时）期间保持高电平。
 */

/* 有效电平：1 表示检测到移动时 OUT 为高电平 */
#define HC_SR501_ACTIVE_LEVEL   1U

/* 上电预热时间（datasheet 建议 30~60s），期间输出不稳定 */
#define HC_SR501_WARMUP_MS      30000U

/* 触发模式：H 可重复触发（默认跳线），L 不可重复触发 */
#define HC_SR501_RETRIGGER_MS   3000U

#endif /* HC_SR501_REGISTER_H */
