/* motion_flow.c 单元测试：加载期流程索引与表达式求值。 */
#include "test.h"
#include "motion_error.h"
#include "motion_flow.h"

static uint32_t flow_offset(const char *text, const char *needle)
{
    const char *p = strstr(text, needle);

    return (p != NULL) ? (uint32_t)(p - text) : 0U;
}

void ut_motion_flow(void)
{
    static const char script[] =
        "LABEL TOP\n"
        "IF 1\n"
        "LOOP 2\n"
        "ENDLOOP\n"
        "ELSE\n"
        "END\n"
        "JOB FAN\n"
        "ENDJOB\n";
    const motion_label_t *label;
    const motion_if_entry_t *if_entry;
    const motion_loop_entry_t *loop;
    const motion_job_entry_t *job;
    motion_variable_table_t vars;
    motion_value_t value;
    uint32_t if_off = flow_offset(script, "IF 1");
    uint32_t else_off = flow_offset(script, "ELSE");
    uint32_t loop_off = flow_offset(script, "LOOP 2");
    uint32_t endloop_off = flow_offset(script, "ENDLOOP");
    uint32_t job_off = flow_offset(script, "JOB FAN");

    tf_begin("motion_flow/流程索引");
    TF_CHECK_INT(motion_flow_build(script), 0);
    TF_CHECK_INT((int)motion_flow_job_count(), 1);
    label = motion_flow_find_label("TOP", 0);
    TF_CHECK(label != NULL);
    if (label != NULL) {
        TF_CHECK_INT((int)label->offset, 0);
        TF_CHECK_INT((int)motion_flow_line_at(label->offset), 1);
    }
    if_entry = motion_flow_find_if(if_off);
    TF_CHECK(if_entry != NULL);
    if (if_entry != NULL) {
        TF_CHECK(if_entry->has_else);
        TF_CHECK_INT((int)if_entry->else_offset, (int)else_off);
    }
    TF_CHECK(motion_flow_find_if(else_off) == NULL);
    TF_CHECK(motion_flow_find_else(else_off) == if_entry);
    loop = motion_flow_find_loop(loop_off);
    TF_CHECK(loop != NULL);
    if (loop != NULL) {
        TF_CHECK_INT((int)loop->endloop_offset, (int)endloop_off);
    }
    job = motion_flow_find_job("FAN");
    TF_CHECK(job != NULL);
    TF_CHECK(motion_flow_job_at(0) == job);
    TF_CHECK(motion_flow_job_at(1) == NULL);
    if (job != NULL) {
        TF_CHECK_INT((int)job->job_offset, (int)job_off);
        TF_CHECK(motion_flow_find_job_by_offset(job_off) == job);
    }
    TF_CHECK(motion_flow_find_job("NOPE") == NULL);
    TF_CHECK(motion_flow_find_label(NULL, 0) == NULL);
    TF_CHECK_INT((int)motion_flow_line_at(0xFFFFU), 0);
    tf_end();

    tf_begin("motion_flow/加载错误");
    TF_CHECK_INT(motion_flow_build(NULL), 0);
    TF_CHECK_INT(motion_flow_build("LABEL A\nLABEL A\n"),
                 MOTION_ERR_FLOW);
    TF_CHECK_INT(motion_flow_build("IF 1\n"), MOTION_ERR_FLOW);
    TF_CHECK_INT(motion_flow_build("LOOP 2\n"), MOTION_ERR_FLOW);
    TF_CHECK_INT(motion_flow_build("JOB X\n"), MOTION_ERR_FLOW);
    TF_CHECK_INT(motion_flow_build("END\n"), MOTION_ERR_FLOW);
    TF_CHECK_INT(motion_flow_build("BREAK\n"), 0);
    tf_end();

    tf_begin("motion_flow/表达式求值");
    motion_variable_init(&vars);
    memset(&value, 0, sizeof(value));
    value.type = MOTION_VALUE_INTEGER;
    value.data.integer = 5;
    TF_CHECK_INT(motion_variable_set(&vars, "A", &value), 0);
    TF_CHECK_INT(motion_flow_eval("$A + 2 * 3", &vars, &value), 0);
    TF_CHECK_INT(value.data.integer, 11);
    TF_CHECK_INT(motion_flow_eval("(1 + 2) * 3", &vars, &value), 0);
    TF_CHECK_INT(value.data.integer, 9);
    TF_CHECK_INT(motion_flow_eval("TRUE AND NOT FALSE", &vars, &value),
                 0);
    TF_CHECK_INT(value.type, MOTION_VALUE_BOOLEAN);
    TF_CHECK(value.data.boolean);
    TF_CHECK_INT(motion_flow_eval("1 > 2 OR 3 >= 3", &vars, &value),
                 0);
    TF_CHECK(value.data.boolean);
    TF_CHECK(motion_flow_eval("$NOPE", &vars, &value) != 0);
    TF_CHECK(motion_flow_eval("(1 + 2", &vars, &value) != 0);
    tf_end();
}
