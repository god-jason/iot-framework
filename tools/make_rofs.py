#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
make_rofs.py - ROFS 1.0 只读镜像生成与查看工具
=============================================================================

用法：
    python tools/make_rofs.py build   <input_dir> <output.bin>
    python tools/make_rofs.py info    <image.bin>
    python tools/make_rofs.py list    <image.bin>
    python tools/make_rofs.py extract <image.bin> <path> [output_file]
    python tools/make_rofs.py verify  <image.bin>

镜像结构（全部小端、紧凑排列、无对齐填充）：

    +---------------------------+
    | Super Header (32B)        |
    +---------------------------+
    | Record 1: Header(16B)     |
    |           Name            |
    |           Body            |
    +---------------------------+
    | Record 2 ...              |
    +---------------------------+
    | END! (4B)                 |
    +---------------------------+

Super Header:
    magic          4B   "ROFS" (0x53464F52)
    version        2B   1
    header_size    2B   32
    image_size     4B   镜像总大小
    record_count   4B   记录数量
    records_offset 4B   第一条记录偏移（32）
    records_size   4B   记录区大小
    image_crc32    4B   整镜像 CRC32（计算时本字段视为 0）
    flags          4B

Record Header:
    type           1B   1=文件，2=目录（本工具只生成文件记录）
    name_len       1B   文件名长度
    flags          2B
    size           4B   Body 大小
    time           4B   Unix 时间戳
    crc32          4B   Body 的 CRC32

CRC32 采用 CRC-32/ISO-HDLC，与 zlib.crc32 一致。
"""

import argparse
import os
import struct
import sys
import zlib

MAGIC = 0x53464F52          # "ROFS"
VERSION = 1
SUPER_SIZE = 32
RECORD_SIZE = 16
END_MAGIC = 0x21444E45      # "END!"

SUPER_FMT = "<IHHIIIIII"
RECORD_FMT = "<BBHIII"

TYPE_FILE = 0x01
TYPE_DIR = 0x02

NAME_MAX = 255
IMAGE_CRC_OFFSET = 24       # Super Header 中 image_crc32 字段偏移


class RofsError(Exception):
    pass


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def check_rel_path(rel: str) -> None:
    """校验相对路径：不以 / 开头或结尾、无空段、无 . 和 .."""
    if not rel:
        raise RofsError("空路径")
    if rel.startswith("/") or rel.endswith("/"):
        raise RofsError("路径不允许以 '/' 开头或结尾: %s" % rel)
    if "\\" in rel:
        raise RofsError("路径只允许 '/' 分隔: %s" % rel)
    for seg in rel.split("/"):
        if seg == "":
            raise RofsError("路径包含空段: %s" % rel)
        if seg in (".", ".."):
            raise RofsError("路径不允许 '.' 或 '..': %s" % rel)
    if len(rel.encode("utf-8")) > NAME_MAX:
        raise RofsError("路径超过 %d 字节: %s" % (NAME_MAX, rel))


def build_image(input_dir: str, output: str, zero_time: bool = False) -> None:
    if not os.path.isdir(input_dir):
        raise RofsError("输入目录不存在: %s" % input_dir)

    entries = []
    seen = set()

    for root, dirs, files in os.walk(input_dir):
        dirs.sort()
        files.sort()
        for name in files:
            full = os.path.join(root, name)
            rel = os.path.relpath(full, input_dir).replace(os.sep, "/")
            check_rel_path(rel)
            if rel in seen:
                raise RofsError("重复路径: %s" % rel)
            seen.add(rel)

            with open(full, "rb") as fp:
                data = fp.read()
            if len(data) > 0xFFFFFFFF:
                raise RofsError("文件超过 4GB: %s" % rel)
            mtime = 0 if zero_time else int(os.stat(full).st_mtime) & 0xFFFFFFFF
            entries.append((rel, data, mtime))

    # 按 UTF-8 字节序排序，使目录项在镜像中连续，便于 MCU 端去重
    entries.sort(key=lambda item: item[0].encode("utf-8"))

    records = bytearray()
    for rel, data, mtime in entries:
        name = rel.encode("utf-8")
        header = struct.pack(
            RECORD_FMT,
            TYPE_FILE,
            len(name),
            0,
            len(data),
            mtime,
            crc32(data),
        )
        records += header
        records += name
        records += data

    records_size = len(records)
    image_size = SUPER_SIZE + records_size + 4

    super_header = bytearray(
        struct.pack(
            SUPER_FMT,
            MAGIC,
            VERSION,
            SUPER_SIZE,
            image_size,
            len(entries),
            SUPER_SIZE,
            records_size,
            0,
            0,
        )
    )
    image = bytes(super_header) + bytes(records) + struct.pack("<I", END_MAGIC)

    # 回填镜像 CRC：计算时 image_crc32 字段为 0
    image_crc = crc32(image)
    image = (image[:IMAGE_CRC_OFFSET] + struct.pack("<I", image_crc) +
             image[IMAGE_CRC_OFFSET + 4:])

    with open(output, "wb") as fp:
        fp.write(image)

    print("build ok: %s" % output)
    print("  files       : %d" % len(entries))
    print("  records size: %d bytes" % records_size)
    print("  image size  : %d bytes (%.1f KB)" % (image_size, image_size / 1024.0))
    print("  image crc32 : 0x%08X" % image_crc)


def parse_image(data: bytes) -> dict:
    if len(data) < SUPER_SIZE + 4:
        raise RofsError("镜像太小")

    (magic, version, header_size, image_size, count,
     records_offset, records_size, image_crc, flags) = struct.unpack_from(
        SUPER_FMT, data, 0)

    if magic != MAGIC:
        raise RofsError("魔数错误: 0x%08X" % magic)
    if version != VERSION:
        raise RofsError("版本不支持: %d" % version)
    if header_size != SUPER_SIZE:
        raise RofsError("header_size 错误: %d" % header_size)
    if image_size != len(data):
        raise RofsError("image_size 与实际长度不一致: %d != %d" %
                        (image_size, len(data)))
    if records_offset < SUPER_SIZE or records_offset > image_size:
        raise RofsError("records_offset 越界")
    if records_size > image_size - records_offset:
        raise RofsError("records_size 越界")
    if records_offset + records_size + 4 != image_size:
        raise RofsError("记录区与 END 标记不匹配")

    end_magic = struct.unpack_from("<I", data, records_offset + records_size)[0]
    if end_magic != END_MAGIC:
        raise RofsError("END 标记错误: 0x%08X" % end_magic)

    records = []
    off = records_offset
    end = records_offset + records_size

    for _ in range(count):
        if off + RECORD_SIZE > end:
            raise RofsError("记录头越界")
        rtype, name_len, rflags, size, time, rcrc = struct.unpack_from(
            RECORD_FMT, data, off)
        if name_len == 0:
            raise RofsError("name_len 为 0")
        name_off = off + RECORD_SIZE
        body_off = name_off + name_len
        if body_off > end or size > end - body_off:
            raise RofsError("记录数据越界")
        name = data[name_off:body_off].decode("utf-8", errors="replace")
        body = data[body_off:body_off + size]
        if crc32(body) != rcrc:
            raise RofsError("记录 CRC 错误: %s" % name)
        records.append({
            "type": rtype,
            "name": name,
            "size": size,
            "time": time,
            "crc32": rcrc,
        })
        off = body_off + size

    if off != end:
        raise RofsError("记录数量与 records_size 不一致")

    # 校验镜像 CRC（字段视为 0）
    zeroed = bytearray(data)
    zeroed[IMAGE_CRC_OFFSET:IMAGE_CRC_OFFSET + 4] = b"\x00\x00\x00\x00"
    if crc32(bytes(zeroed)) != image_crc:
        raise RofsError("镜像 CRC 错误")

    return {
        "version": version,
        "image_size": image_size,
        "records_offset": records_offset,
        "records_size": records_size,
        "count": count,
        "image_crc": image_crc,
        "flags": flags,
        "records": records,
    }


def read_image(path: str) -> bytes:
    with open(path, "rb") as fp:
        return fp.read()


def cmd_info(args) -> int:
    info = parse_image(read_image(args.image))
    print("image      : %s" % args.image)
    print("version    : %d" % info["version"])
    print("image_size : %d bytes" % info["image_size"])
    print("records    : %d @ offset %d, size %d" %
          (info["count"], info["records_offset"], info["records_size"]))
    print("image_crc  : 0x%08X" % info["image_crc"])
    print("flags      : 0x%08X" % info["flags"])
    return 0


def cmd_list(args) -> int:
    info = parse_image(read_image(args.image))
    for rec in info["records"]:
        kind = "DIR " if rec["type"] == TYPE_DIR else "FILE"
        print("%s %8d  %s" % (kind, rec["size"], rec["name"]))
    return 0


def cmd_extract(args) -> int:
    data = read_image(args.image)
    info = parse_image(data)
    want = args.path.lstrip("/")
    off = info["records_offset"]
    for rec in info["records"]:
        name_len = len(rec["name"].encode("utf-8"))
        body_start = off + RECORD_SIZE + name_len
        if rec["name"] == want:
            body = data[body_start:body_start + rec["size"]]
            if args.output:
                with open(args.output, "wb") as fp:
                    fp.write(body)
                print("extracted %d bytes -> %s" % (len(body), args.output))
            else:
                sys.stdout.buffer.write(body)
            return 0
        off = body_start + rec["size"]
    raise RofsError("文件不存在: %s" % args.path)


def cmd_verify(args) -> int:
    try:
        info = parse_image(read_image(args.image))
    except RofsError as exc:
        print("verify FAILED: %s" % exc)
        return 1
    print("verify ok: %d records, %d bytes, crc 0x%08X" %
          (info["count"], info["image_size"], info["image_crc"]))
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="ROFS 1.0 只读镜像工具")
    sub = parser.add_subparsers(dest="cmd")

    p_build = sub.add_parser("build", help="把目录打包成 ROFS 镜像")
    p_build.add_argument("input", help="输入目录")
    p_build.add_argument("output", help="输出 .bin 文件")
    p_build.add_argument("--zero-time", action="store_true",
                         help="时间字段统一写 0，便于可重复构建")
    p_build.set_defaults(func=lambda a: (build_image(a.input, a.output,
                                                     a.zero_time), 0)[1])

    p_info = sub.add_parser("info", help="查看镜像信息")
    p_info.add_argument("image")
    p_info.set_defaults(func=cmd_info)

    p_list = sub.add_parser("list", help="列出镜像中的文件")
    p_list.add_argument("image")
    p_list.set_defaults(func=cmd_list)

    p_extract = sub.add_parser("extract", help="提取单个文件")
    p_extract.add_argument("image")
    p_extract.add_argument("path")
    p_extract.add_argument("output", nargs="?")
    p_extract.set_defaults(func=cmd_extract)

    p_verify = sub.add_parser("verify", help="校验镜像完整性")
    p_verify.add_argument("image")
    p_verify.set_defaults(func=cmd_verify)

    args = parser.parse_args(argv)
    if not getattr(args, "func", None):
        parser.print_help()
        return 2

    try:
        return args.func(args)
    except RofsError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
