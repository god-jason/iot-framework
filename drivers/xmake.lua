-- drivers/xmake.lua - 外设驱动集合（静态库，按需链接）
ros_object("ros-drivers", {
    kind = "static",
    scope = "both",
    files = { "**.c" },
})
