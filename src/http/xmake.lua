-- src/http/xmake.lua - 通用 HTTP（ML307 目标改用 modules/ml307 原生实现）
ros_object("ros-cmp-http", {
    scope = "both",
    files = { "*.c" },
    exclude = is_plat("cross") and { "http_client.c" } or nil,
})
