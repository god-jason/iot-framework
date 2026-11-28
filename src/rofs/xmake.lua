-- src/rofs/xmake.lua - ROFS 1.0 只读文件系统
-- 镜像由 tools/make_rofs.py 生成，运行时只读；host 与 cross 均编译。
ros_object("ros-cmp-rofs", { scope = "both", files = { "*.c" } })
