/* drive 单元测试：串级闭环在速度型被控对象上的收敛性。 */
#include "test.h"
#include "drive.h"

/* 简化被控对象：输出即速度，位置为速度积分 */
typedef struct {
    float pos;
    float last_output;
} sim_plant_t;

static int sim_read_position(void *ctx, float *pos)
{
    *pos = ((sim_plant_t *)ctx)->pos;
    return 0;
}

static int sim_write_output(void *ctx, float out)
{
    ((sim_plant_t *)ctx)->last_output = out;
    return 0;
}

void ut_drive(void)
{
    sim_plant_t plant;
    drive_t drive;
    drive_ops_t ops;
    float pos;
    float out;
    int i;

    memset(&plant, 0, sizeof(plant));
    ops.read_position = sim_read_position;
    ops.write_output = sim_write_output;
    ops.ctx = &plant;

    tf_begin("drive/初始化与参数校验");
    TF_CHECK_INT(drive_init(NULL, &ops, 1, 0, 0, 1, 0, 0), -1);
    TF_CHECK_INT(drive_init(&drive, NULL, 1, 0, 0, 1, 0, 0), -1);
    TF_CHECK_INT(drive_init(&drive, &ops, 4.0f, 0.2f, 0.0f, 1.0f, 0.0f, 0.0f), 0);
    TF_CHECK_INT(drive_set_limits(&drive, 2.0f, -1.0f, 1.0f), 0);
    TF_CHECK_INT(drive_set_limits(&drive, 0.0f, -1.0f, 1.0f), -1);
    TF_CHECK_INT(drive_set_limits(&drive, 1.0f, 1.0f, -1.0f), -1);
    TF_CHECK_INT(drive_position(&drive, &pos), 0);
    TF_CHECK_NEAR(pos, 0.0f, 1e-6);
    tf_end();

    tf_begin("drive/位置闭环收敛");
    TF_CHECK_INT(drive_reset(&drive, 0.0f), 0);
    TF_CHECK_INT(drive_set_target(&drive, 1.0f), 0);
    for (i = 0; i < 5000; i++) {              /* 5s，dt=1ms */
        out = drive_update(&drive, 0.001f);
        plant.pos += out * 0.001f;
        TF_CHECK(out >= -1.0001f && out <= 1.0001f);
    }
    TF_CHECK_INT(drive_position(&drive, &pos), 0);
    TF_CHECK_NEAR(pos, 1.0f, 0.05);
    tf_end();

    tf_begin("drive/异常输入安全返回");
    out = drive_update(&drive, 0.0f);
    TF_CHECK(out == plant.last_output);
    drive_set_target(&drive, 0.5f);
    drive_reset(&drive, 0.0f);
    plant.pos = 0.0f;
    for (i = 0; i < 3000; i++) {
        out = drive_update(&drive, 0.001f);
        plant.pos += out * 0.001f;
    }
    TF_CHECK_NEAR(plant.pos, 0.5f, 0.05);
    tf_end();
}
