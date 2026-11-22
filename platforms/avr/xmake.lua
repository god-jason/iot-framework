-- platforms/avr/xmake.lua - AVR 平台层（当前默认不参与构建）
ros_object("ros-platform-avr", {
    scope = "none",
    files = { "*.c" },
    defines = { "ROS_PLATFORM_AVR" },
    default = false,
})
