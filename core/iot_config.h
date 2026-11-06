#ifndef IOT_CONFIG_H
#define IOT_CONFIG_H

/*
 * 框架总配置文件（编译期裁剪宏）。
 * 各宏只决定对应模块是否参与编译，不替代运行时能力检测。
 */

/* ---- Core ---- */
#define IOT_USE_CORE             1
#define IOT_USE_COMPONENT        1
#define IOT_USE_EVENT            1
#define IOT_USE_TIMER            1
#define IOT_USE_SCHEDULER        1
#define IOT_USE_SERVICE          1
#define IOT_USE_DEVICE           1

/* ---- Container ---- */
#define IOT_USE_LIST             1
#define IOT_USE_QUEUE            1
#define IOT_USE_RINGBUF          1
#define IOT_USE_HEAP             1
#define IOT_USE_HASHMAP          1
#define IOT_USE_TREE             0
#define IOT_USE_MEMORY_POOL      1

/* ---- Algorithm ---- */
#define IOT_USE_FILTER           1
#define IOT_USE_PID              1
#define IOT_USE_KALMAN           0
#define IOT_USE_MOTION           1
#define IOT_USE_STATISTICS       1

/* ---- Utils ---- */
#define IOT_USE_HEX              1
#define IOT_USE_BASE64           1
#define IOT_USE_BCD              1
#define IOT_USE_JSON             1
#define IOT_USE_CRC              1
#define IOT_USE_SHA256           1

/* ---- Protocol ---- */
#define IOT_USE_AT               1
#define IOT_USE_MQTT             1
#define IOT_USE_HTTP             1
#define IOT_USE_MODBUS           0

/* ---- Components ---- */
#define IOT_USE_ML307            1
#define IOT_USE_BUTTON           1
#define IOT_USE_MOTOR            1
#define IOT_USE_SERVO            1
#define IOT_USE_ADC              1

/* ---- Robot ---- */
#define IOT_USE_FSM              1
#define IOT_USE_ACTION           1
#define IOT_USE_BEHAVIOR         1
#define IOT_USE_PLANNER          1

/* ---- System ---- */
#define IOT_USE_LOGGER           1
#define IOT_USE_SHELL            1
#define IOT_USE_WATCHDOG         1
#define IOT_USE_KV               1
#define IOT_USE_OTA              1

#endif /* IOT_CONFIG_H */