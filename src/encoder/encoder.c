/*
 * 编码器模块实现。
 *
 * 职责：
 *   通过底层回调读取增量式编码器的累计脉冲数，并在给定时间间隔上
 *   差分得到转速（脉冲/秒）。
 *
 * 数据流：
 *   init() 绑定 read_count 回调 -> read() 读取硬件计数 ->
 *   get_speed(dt) 对 count 做差分并除以 dt，同时更新 last_count。
 *
 * 约束与边界：
 *   - 计数为带符号 32 位，正负对应两个旋转方向；
 *   - dt 必须大于 0，否则无法计算速度；
 *   - 计数差分存在 32 位回绕的可能，调用者应保证采样间隔内不溢出；
 *   - 本模块不做滤波，噪声较大时应在上层做滑动平均。
 */
#include "encoder.h"
#include "log.h"

/*
 * 初始化编码器对象。
 * 参数：enc 对象指针（非 NULL）；ops 操作表（read_count 必须实现）。
 * 返回：0 成功；参数非法返回 -1。
 * 说明：计数、上次计数与速度均清零。
 */
int encoder_init(encoder_t *enc, const encoder_ops_t *ops)
{
    if (enc == NULL || ops == NULL || ops->read_count == NULL) {
        return -1;
    }
    enc->ops = ops;
    enc->count = 0;
    enc->last_count = 0;
    enc->speed = 0.0f;
    return 0;
}

/*
 * 读取当前累计脉冲数。
 * 参数：enc 对象指针；count 输出缓冲。
 * 返回：0 成功；参数非法或底层读取失败返回 -1。
 * 说明：成功时同时把最新计数缓存到 enc->count。
 */
int encoder_read(encoder_t *enc, int32_t *count)
{
    if (enc == NULL || count == NULL) {
        return -1;
    }
    if (enc->ops->read_count(enc->ops->ctx, &enc->count) != 0) {
        LOG_E("encoder", "read fail");   /* 读取计数失败 */
        return -1;
    }
    *count = enc->count;
    return 0;
}

/*
 * 计算转速。
 * 参数：enc 对象指针；dt 距上次采样的时间（秒，必须 > 0）；speed 输出缓冲。
 * 返回：0 成功；参数非法或读取失败返回 -1。
 * 说明：speed = (count - last_count) / dt，单位脉冲/秒；
 *       计算完成后立即用当前计数更新 last_count，形成差分基准。
 */
int encoder_get_speed(encoder_t *enc, float dt, float *speed)
{
    if (enc == NULL || speed == NULL || dt <= 0.0f) {
        return -1;
    }
    if (encoder_read(enc, &enc->count) != 0) {
        return -1;
    }
    /* 差分计数 / 采样间隔 */
    enc->speed = (float)(enc->count - enc->last_count) / dt;
    enc->last_count = enc->count;
    *speed = enc->speed;
    return 0;
}

/*
 * 复位编码器。
 * 参数：enc 对象指针。
 * 返回：0 成功；enc 为 NULL 返回 -1。
 * 说明：先把 last_count 同步为当前 count，再调用可选底层 reset。
 *       注意：若底层 reset 把硬件计数清零，则这里的同步会让下一次
 *       差分为负值，调用者应在复位后重新建立基准。
 */
int encoder_reset(encoder_t *enc)
{
    if (enc == NULL) {
        return -1;
    }
    enc->last_count = enc->count;
    if (enc->ops->reset != NULL) {
        enc->ops->reset(enc->ops->ctx);
    }
    return 0;
}