-- platforms/stm32f1/xmake.lua - STM32F1 外设库与 F103CB 板级对象
-- 公共实现见 platforms/stm32/xmake.lua；最终固件/Bootloader 目标定义在
-- apps/f103cb/xmake.lua。
if is_plat("cross") and ROS_FAMILY == "f1" then
    local board = "platforms/stm32f1"
    local app_dir = path.join(ROS_ROOT, "apps/f103cb")
    local dfp_inc = ROS_DFP_INC or ""
    local cmsis_inc = ROS_CMSIS_INC or ""
    local incs = { path.join(ROS_ROOT, board), app_dir }
    if dfp_inc ~= "" then table.insert(incs, dfp_inc) end
    if cmsis_inc ~= "" then table.insert(incs, cmsis_inc) end

    -- 寄存器外设实现（板级文件由下方 board 目标单独编译）
    ros_object("ros-platform-stm32f1", {
        scope = "cross",
        files = { "*.c" },
        exclude = { "board.c", "board_flash.c", "board_rtc.c" },
        defines = { "STM32F10X_MD", "HSE_VALUE=8000000" },
        includedirs = incs,
    })

    -- App 板级对象：启动文件 + board*.c + 平台共享 boot/config/ota 实现
    --（共享实现依赖本板 partition.h，故由板级目标编译）
    ros_object("board-f103cb", {
        scope = "none",
        files = { "board.c", "board_flash.c", "board_rtc.c",
                  path.join(app_dir, "startup_stm32f10x_md.S"),
                  path.join(ROS_ROOT, "platforms/stm32/stm32_main.c"),
                  path.join(ROS_ROOT, "platforms/stm32/bootloader/boot_kv.c"),
                  path.join(ROS_ROOT, "platforms/stm32/bootloader/boot_meta.c"),
                  path.join(ROS_ROOT, "platforms/stm32/stm32_ota.c") },
        defines = { "STM32F10X_MD", "HSE_VALUE=8000000" },
        includedirs = incs,
    })

    -- Bootloader 板级对象（不依赖内核 / libc：boot_mem.c 提供最小 libc 子集）
    ros_object("boot-f103cb", {
        scope = "none",
        files = { path.join(app_dir, "startup_stm32f10x_md.S"), "board_flash.c",
                  path.join(ROS_ROOT, "platforms/stm32/bootloader/bootloader.c"),
                  path.join(ROS_ROOT, "platforms/stm32/bootloader/boot_kv.c"),
                  path.join(ROS_ROOT, "platforms/stm32/bootloader/boot_meta.c"),
                  path.join(ROS_ROOT, "platforms/stm32/bootloader/boot_mem.c"),
                  path.join(ROS_ROOT, "components/crypto/crc32.c") },
        defines = { "STM32F10X_MD", "HSE_VALUE=8000000" },
        includedirs = incs,
        cflags = { "-ffreestanding" },
    })
end
