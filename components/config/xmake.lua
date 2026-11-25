-- components/config/xmake.lua
-- 存储核心：直接调用 components/fkv，不依赖 roshell
ros_object("ros-cmp-config", { scope = "both", files = { "config.c" } })
