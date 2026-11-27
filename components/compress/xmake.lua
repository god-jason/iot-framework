-- components/compress/xmake.lua - 压缩算法：LZSS / DEFLATE / RLE
ros_object("ros-cmp-compress", {
    scope = "both",
    files = { "*.c" },
})
