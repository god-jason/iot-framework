#ifndef AM312_REGISTER_H
#define AM312_REGISTER_H

/*
 * AM312 人体红外（PIR）传感器电气/时序参数。
 *
 * 模块内部带数字输出（推挽），检测到人体移动时 OUT 拉高并保持一段时间。
 */

/* 有效电平：1 表示检测到移动时 OUT 为高电平 */
#define AM312_ACTIVE_LEVEL      1U

/* 上电预热时间：期间输出不稳定，调用方应等待后再判定 */
#define AM312_WARMUP_MS         1000U

/* 输出保持（延时）时间典型值，仅供参考 */
#define AM312_HOLD_MS           2000U

#endif /* AM312_REGISTER_H */
