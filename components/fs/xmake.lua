-- components/fs/xmake.lua - littlefs 文件接口封装
ros_object("ros-cmp-fs", {
    scope = "none",
    files = { "*.c" },
    includedirs = { path.join(ROS_ROOT, "vendor/littlefs") },
})
