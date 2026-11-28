-- src/net/xmake.lua - 网络抽象（ML307 目标用 modules/ml307 的 MIP 实现）
ros_object("ros-cmp-net", {
    scope = "both",
    files = { "*.c" },
    exclude = is_plat("cross") and { "net_bsd.c" } or nil,
})
