/*
 * drive_test.c - 闭环伺服测试（对应 drive.c）
 *
 * 覆盖：初始化校验、限幅设置、位置闭环收敛、复位、异常输入。
 * 被控对象简化为「输出即速度、位置为速度积分」。
 */

#include "test.h"
#include "drive.h"

typedef struct {
    float pos;
    float last_output;
} plant_t;

static int plant_read(void *ctx, float *pos)
{
    *pos = ((plant_t *)ctx)->pos;
    return 0;
}

static int plant_write(void *ctx, float out)
{
    ((plant_t *)ctx)->last_output = out;
    return 0;
}

void ut_drive(void)
{
    plant_t plant;
    drive_t drive;
    drive_ops_t ops;
    float pos = 0.0f;
    float out;
    int i;

    memset(&plant, 0, sizeof(plant));
    ops.read_position = plant_read;
    ops.write_output = plant_write;
    ops.ctx = &plant;

    tf_begin("drive/初始化与限幅");

    TF_CHECK_INT(drive_init(NULL, &ops, 1, 0, 0, 1, 0, 0), -1);
    TF_CHECK_INT(drive_init(&drive, NULL, 1, 0, 0, 1, 0, 0), -1);
    {
        drive_ops_t bad;
        bad.read_position = NULL;
        bad.write_output = plant_write;
        bad.ctx = &plant;
        TF_CHECK_INT(drive_init(&drive, &bad, 1, 0, 0, 1, 0, 0), -1);
    }
    TF_CHECK_INT(drive_init(&drive, &ops, 4.0f, 0.2f, 0.0f,
                            1.0f, 0.0f, 0.0f), 0);

    TF_CHECK_INT(drive_set_limits(&drive, 2.0f, -1.0f, 1.0f), 0);
    TF_CHECK_INT(drive_set_limits(&drive, 0.0f, -1.0f, 1.0f), -1);
    TF_CHECK_INT(drive_set_limits(&drive, 1.0f, 1.0f, -1.0f), -1);

    TF_CHECK_INT(drive_position(&drive, &pos), 0);
    TF_CHECK_NEAR(pos, 0.0f, 1e-6);
    TF_CHECK_INT(drive_position(&drive, NULL), -1);

    tf_end();

    tf_begin("drive/位置闭环收敛");

    TF_CHECK_INT(drive_reset(&drive, 0.0f), 0);
    TF_CHECK_INT(drive_set_target(&drive, 1.0f), 0);

    for (i = 0; i < 5000; i++) {              /* 5 s，dt = 1 ms */
        out = drive_update(&drive, 0.001f);
        plant.pos += out * 0.001f;
        TF_CHECK(out >= -1.0001f && out <= 1.0001f);
    }
    TF_CHECK_INT(drive_position(&drive, &pos), 0);
    TF_CHECK_NEAR(pos, 1.0f, 0.05);

    tf_end();

    tf_begin("drive/反向与复位");

    TF_CHECK_INT(drive_set_target(&drive, -0.5f), 0);
    for (i = 0; i < 5000; i++) {
        out = drive_update(&drive, 0.001f);
        plant.pos += out * 0.001f;
    }
    TF_CHECK_NEAR(plant.pos, -0.5f, 0.05);

    plant.pos = 2.0f;
    TF_CHECK_INT(drive_reset(&drive, 2.0f), 0);
    TF_CHECK_INT(drive_position(&drive, &pos), 0);
    TF_CHECK_NEAR(pos, 2.0f, 1e-6);

    tf_end();

    tf_begin("drive/异常输入");

    /* dt <= 0 不更新状态，返回上一次输出 */
    out = drive_update(&drive, 0.0f);
    TF_CHECK_NEAR(out, drive.output, 1e-6);
    TF_CHECK(drive_update(NULL, 0.001f) == 0.0f);
    TF_CHECK_INT(drive_set_target(NULL, 1.0f), -1);

    tf_end();
}
