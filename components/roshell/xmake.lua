-- components/roshell/xmake.lua - 交互式命令行
-- 源文件、头文件与单元测试平铺在组件根目录；
-- *_test.c 由 ros_object 默认排除，仅由 ros-test 递归收集。
-- 内置数学函数默认关闭（见 roshell_config.h 的 RS_MATH_ENABLE）；
-- 主机单元测试仍启用，保持数学用例覆盖。
ros_object("ros-cmp-roshell", {
    scope = "both",
    files = { "*.c" },
    defines = (not is_plat("cross")) and { "RS_MATH_ENABLE=1" } or nil,
})
