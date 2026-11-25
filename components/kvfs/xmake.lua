-- components/kvfs/xmake.lua - kvfs 双区滚动日志键值存储
-- 只依赖注入的块设备回调，host 与 cross 均编译；测试文件由 ros-test 收集。
ros_object("ros-cmp-kvfs", { scope = "both", files = { "*.c" } })
