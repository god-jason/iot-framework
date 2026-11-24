-- components/config/xmake.lua
-- 存储核心：不依赖 libc/roshell，可编入 -nostdlib 的 Bootloader
ros_object("ros-cmp-config", { scope = "both", files = { "config.c" } })
-- 通用 cfg 命令：依赖 roshell，只进入应用固件/主机目标（Bootloader 不依赖本目标）
ros_object("ros-cmp-config-cmd", { scope = "both", files = { "config_cmd.c" } })
