#!/usr/bin/env python
# -*- Mode: python; tab-width: 4; indent-tabs-mode: nil; coding: utf-8; -*-
# SPDX-License-Identifier: MIT

import os
import sys
import fcntl
import hashlib
from ctypes import *
from pathlib import Path
from contextlib import suppress

try:
    from PIL import Image, ImageDraw
except ImportError:
    print('require PIL')
    sys.exit(1)


libc = CDLL('libc.so.6')
libc.ioctl.argtypes = [c_int, c_ulong, c_void_p]

MAX_EXTENT = 256
FS_IOC_FIEMAP = 0xc020660b

# from '/usr/include/linux/fiemap.h'
class FiemapExtent(Structure):
    _fields_ = [('fe_logical', c_uint64),
                ('fe_physical', c_uint64),
                ('fe_length', c_uint64),
                ('fe_reserved64', c_uint64 * 2),
                ('fe_flags', c_uint32),
                ('fe_reserved', c_uint32 * 3)]

class Fiemap(Structure):
    _pack_ = 8
    _fields_ = [('fm_start', c_uint64),
                ('fm_length', c_uint64),
                ('fm_flags', c_uint32),
                ('fm_mapped_extents', c_uint32),
                ('fm_extent_count', c_uint32),
                ('fm_extents', FiemapExtent * MAX_EXTENT)]

    def init(self):
        self.fm_start = 0
        self.fm_extent_count = (sizeof(Fiemap) - sizeof(FiemapExtent)) // sizeof(FiemapExtent)
        self.fm_length = (2 ** 64) - 1
        self.fm_flags = 0


def get_extents(path, fm=None):
    if not fm:
        fm = Fiemap()
    fm.init()
    fd = os.open(path, os.O_RDONLY)
    if fd == -1:
        return None
    if libc.ioctl(fd, FS_IOC_FIEMAP, addressof(fm)) != 0:
        os.close(fd)
        return None
    os.close(fd)
    stat = path.stat()
    #  (devid, inode, [(physical, length)])
    return (stat.st_dev, stat.st_ino,
                [(fm.fm_extents[i].fe_physical, fm.fm_extents[i].fe_length) for i in range(fm.fm_mapped_extents)])

def fill(im, extents, width, path):
    draw = ImageDraw.Draw(im)
    block_denom = 1024 * 1024 * 4
    try:
        digest = hashlib.md5(str(path.stat().st_ino).encode()).digest()
    except (PermissionError, FileNotFoundError):
        print(f'Error: {path}')
        return
    color = (64 + digest[0] % 190,
             64 + digest[1] % 190,
             64 + digest[2] % 190)
    for (physical, length) in extents[2]:
        offset = 0 #250e9
        block = (physical - offset) // block_denom
        y = block // width
        x = block % width
        length = length // block_denom
        draw.line((x, y, x + length, y), fill=color)
        while (x + length) >= width:
            y += 1
            length -= width - x
            x = 0
            draw.line((x, y, x + length, y), fill=color)

devid = -1

def walk(path):
    global devid
    if path.is_symlink():
        return
    try:
        if devid == -1:
            devid = path.stat().st_dev
        if devid == path.stat().st_dev:
            if path.is_file():
                yield path
            elif path.is_dir():
                for p in path.iterdir():
                    yield from walk(p)
    except PermissionError:
        print(f'PermissionError: {path}')
    except OSError as e:
        print(f'Error {e}')

def main():
    import datetime as dt
    inodes = set()
    block_denom = 1024 * 1024 * 4
    width = None
    device = None
    dev_size = -1
    im = None
    fm = Fiemap()
    output_filename = '{0:%Y}_{0:%m}_{0:%d}_{0:%H}_{0:%M}.png'.format(dt.datetime.now())
    for path in walk(Path(sys.argv[1])):
        try:
            extents = get_extents(path, fm)
        except (PermissionError, FileNotFoundError):
            continue
        if not device:
            device = extents[0]
            dev_major = (device >> 8) & 0xff
            dev_minor = device & 0xff
            dev_size = int(Path(f'/sys/dev/block/{dev_major}:{dev_minor}/size').read_text()) * 512
            print('dev_size:', dev_size // 1024 // 1024 // 1024, 'GiB')
            # yes I'm too lazy
            width = 200 if dev_size < (1024 * 1024 * 1024 * 1024) else 600
            im = Image.new('RGB', (width, dev_size // (4*1024*1024) // width), (1, 5, 8))
        if extents[1] in inodes:
            continue
        inodes.add(extents[1])
        fill(im, extents, width, path)
    im.save(output_filename)

if __name__ == '__main__':
    main()

