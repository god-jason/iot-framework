#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ota.py - OTA 文件格式基础代码（包头 + 打包）

与 src/fota/fota.h 保持一致（32 字节，小端）：

    magic(4) version(2) flags(2) image_len(4) image_crc(4)
    data_len(4) data_crc(4) reserved(4) hdr_crc(4)

    - flags = 0       负载为原始镜像（data_len == image_len）
    - flags = 0x0001  负载为 RAW DEFLATE（RFC1951，无 zlib/gzip 头）
    - hdr_crc 覆盖包头前 28 字节

tools/build_ota.py（离线生成 .ota）与 tools/uart_ota.py（串口烧写）
共用本模块，避免两处包头/CRC 实现漂移。
"""

from __future__ import annotations

import struct
import zlib

# OTA 包头（与 src/fota/fota.h 一致，小端）
FOTA_HDR_MAGIC = 0x41544F46
FOTA_HDR_VERSION = 1
FOTA_HDR_SIZE = 32
FOTA_FLAG_DEFLATE = 0x0001


def crc32(data: bytes) -> int:
    """标准 CRC-32/IEEE（多项式 0xEDB88320），与 MCU 端逐位实现等价。"""
    return zlib.crc32(data) & 0xFFFFFFFF


def build_package(image: bytes, compress: bool) -> tuple[bytes, int, int]:
    """按 src/fota/fota.h 打包：32B 包头 + 负载。

    返回 (完整升级包, 镜像 CRC32, 压缩后负载长度)。
    """
    image_len = len(image)
    image_crc = crc32(image)
    if compress:
        co = zlib.compressobj(9, zlib.DEFLATED, -15)
        payload = co.compress(image) + co.flush()
        flags = FOTA_FLAG_DEFLATE
    else:
        payload = image
        flags = 0

    data_crc = crc32(payload)
    hdr = struct.pack("<IHHIIIIII", FOTA_HDR_MAGIC, FOTA_HDR_VERSION, flags,
                      image_len, image_crc, len(payload), data_crc, 0, 0)
    hdr = hdr[:28] + struct.pack("<I", crc32(hdr[:28]))
    return hdr + payload, image_crc, len(payload)
