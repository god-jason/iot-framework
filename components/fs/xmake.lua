-- components/fs/xmake.lua - 通用文件系统接口 + VFS + littlefs 后端
-- 测试文件 fs_test.c 由 ros_object 默认排除，仅进入 ros-test。
ros_object("ros-cmp-fs", { scope = "both", files = { "*.c" } })
