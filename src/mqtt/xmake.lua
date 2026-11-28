-- src/mqtt/xmake.lua - 通用 MQTT（ML307 目标改用 modules/ml307 原生实现）
ros_object("ros-cmp-mqtt", {
    scope = "both",
    files = { "*.c" },
    exclude = is_plat("cross") and { "mqtt.c" } or nil,
})
