-- vendor/littlefs/xmake.lua - littlefs v2.9.3（嵌入式裁剪配置）
-- 参与默认构建，由内核堆后端提供分配器；未被 VFS 引用时链接期会被 gc-sections 丢弃。
ros_object("ros-vendor-littlefs", {
    scope = "both",
    files = { "lfs.c", "lfs_util.c" },
    cflags = { "-include", "ros_mem.h" },
    defines = {
        "LFS_MALLOC=ros_mem_malloc",  -- 统一走内核堆
        "LFS_FREE=ros_mem_free",
        "LFS_NO_ASSERT",
        "LFS_NO_DEBUG",
        "LFS_NO_WARN",
        "LFS_NO_ERROR",
        "LFS_NAME_MAX=63",            -- 与 FS_NAME_MAX 保持一致
    },
})
