-- src/mfs/xmake.lua - MFS（原 ROFS）日志结构文件系统
-- 由各 app_fs.c 中的本地适配层挂载到 VFS（src/fs）。
ros_object("ros-cmp-mfs", { scope = "both", files = { "*.c" } })
