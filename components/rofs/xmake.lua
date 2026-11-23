-- components/rofs/xmake.lua
-- ROFS 日志结构文件系统：纯 C99、无外部依赖，host 与 cross 均编译。
-- 测试文件 rofs_test.c 由 ros_object 默认排除，仅进入 ros-test。
ros_object("ros-cmp-rofs", { scope = "both", files = { "*.c" } })
