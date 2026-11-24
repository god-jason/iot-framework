-- vendor/littlefs/xmake.lua - littlefs v2.9.3（嵌入式裁剪配置）
-- 当前未通过 VFS 使用，scope=none 不参与默认构建，保留供后续接入。
ros_object("ros-vendor-littlefs", {
    scope = "none",
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
