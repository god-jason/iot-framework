-- components/roshell/xmake.lua - 交互式命令行
-- 源文件、头文件与单元测试平铺在组件根目录；
-- *_test.c 由 ros_object 默认排除，仅由 ros-test 递归收集。
ros_object("ros-cmp-roshell", {
    scope = "both",
    files = { "*.c" },
})
