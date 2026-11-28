-- src/rtos/xmake.lua - 实时内核（目标：ros-core）
ros_object("ros-core", {
    scope = "both",
    files = { "*.c" },
})
