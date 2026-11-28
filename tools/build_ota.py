#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""build_ota.py - 把原始固件打包成带 fota 包头的 OTA 文件

包头格式与 components/fota/fota.h 完全一致（32 字节，小端）：

    magic(4) version(2) flags(2) image_len(4) image_crc(4)
    data_len(4) data_crc(4) reserved(4) hdr_crc(4)

    - flags = 0       负载为原始镜像（data_len == image_len）
    - flags = 0x0001  负载为 RAW DEFLATE（RFC1951，无 zlib/gzip 头）
    - hdr_crc 覆盖包头前 28 字节

生成的 .ota 文件是「可直接写入 OTA 区」的完整包，也可用企业 OTA 平台/离线
工具分发。注意 tools/uart_ota.py 接收的是原始 bin 并会自行加包头，不要把
本脚本的输出再传给 uart_ota.py（否则会被二次封装）。

典型用法：
    # 原始镜像（不压缩）
    python tools/build_ota.py --bin build/cross/arm/release/f407ve.bin

    # DEFLATE 压缩（Bootloader 需启用 FLASH_OTA_COMPRESS，当前 F407VE）
    python tools/build_ota.py --compress \
        --bin build/cross/arm/release/f407ve.bin

    # 指定输出文件
    python tools/build_ota.py -o fw.ota --bin xxx.bin

依赖：Python 3（仅标准库）
"""

from __future__ import annotations

import argparse
import os
import sys

from ota import FOTA_FLAG_DEFLATE, FOTA_HDR_SIZE, build_package, crc32


def default_output(bin_path: str) -> str:
    root, ext = os.path.splitext(bin_path)
    if ext.lower() == ".bin":
        return root + ".ota"
    return bin_path + ".ota"


def main() -> int:
    ap = argparse.ArgumentParser(
        description="把固件 bin 打包为带 fota 包头的 OTA 文件",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    ap.add_argument("--bin", required=True, help="原始固件 bin（按 APP 区地址链接）")
    ap.add_argument("-o", "--output", default=None,
                    help="输出文件；默认为 bin 同名 .ota")
    ap.add_argument("--compress", action="store_true",
                    help="用 raw deflate 压缩负载（flags=DEFLATE）")
    args = ap.parse_args()

    try:
        with open(args.bin, "rb") as f:
            image = f.read()
    except OSError as exc:
        print(f"读取固件失败：{exc}", file=sys.stderr)
        return 2

    if not image:
        print("固件为空", file=sys.stderr)
        return 2

    package, image_crc, data_len = build_package(image, args.compress)
    out_path = args.output or default_output(args.bin)

    try:
        with open(out_path, "wb") as f:
            f.write(package)
    except OSError as exc:
        print(f"写入 {out_path} 失败：{exc}", file=sys.stderr)
        return 2

    payload = package[FOTA_HDR_SIZE:]
    flags = FOTA_FLAG_DEFLATE if args.compress else 0
    print(f"固件: {args.bin}")
    print(f"镜像: {len(image)} 字节  CRC32: 0x{image_crc:08X}")
    print(f"格式: {'raw deflate' if args.compress else '原始镜像'}  "
          f"flags=0x{flags:04X}")
    print(f"负载: {data_len} 字节  CRC32: 0x{crc32(payload):08X}")
    if args.compress and image:
        print(f"压缩率: {data_len * 100.0 / len(image):.2f}%")
    print(f"升级包: {len(package)} 字节（32B 包头 + 负载）  CRC32: "
          f"0x{crc32(package):08X}")
    print(f"输出: {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
