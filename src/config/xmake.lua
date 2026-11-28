-- src/config/xmake.lua
-- 存储核心：直接调用 src/fkv；config_cmd.c 提供 cfg 命令（依赖 roshell）
ros_object("ros-cmp-config", {
    scope = "both",
    files = { "config.c", "config_cmd.c" },
})
