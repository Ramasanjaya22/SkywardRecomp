"""Shader preparation's index of the game files' shaders, on a made-up RomFS image (an .nsp setup: romfs.bin, no
romfs/ folder) with a made-up Shaders.arc.gz (gzip members around a U8 archive of GLSLC outputs); no game files are used."""
import gzip
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
source = (ROOT / 'runtime/nvn/shader_prep.c').read_text()
start = source.index('uint64_t shader_code_hash(')
hash_fn = source[start:source.index('\n}\n', start) + 3]
start = source.index('// ------------------------------------------------------------------ index of the game files')
index = source[start:source.index('// ------------------------------------------------------------------ cache tags', start)]


def bnsh(seed):
    """One BNSH container with one program whose vertex stage's code is 0x60 bytes (0x40 code + 0x20 constants)."""
    d = bytearray(0x1100)
    d[0:8] = b'BNSH\0\0\0\0'
    d[0xC:0xE] = b'\xff\xfe'
    struct.pack_into('<H', d, 0x16, 0x20)      # first block: the container
    struct.pack_into('<I', d, 0x1C, len(d))    # file size
    d[0x20:0x24] = b'grsc'
    struct.pack_into('<IQ', d, 0x3C, 1, 0x80)  # 1 variation at 0x80
    struct.pack_into('<Q', d, 0x90, 0x100)     # its program
    struct.pack_into('<Q', d, 0x108, 0x140)    # the program's vertex stage
    struct.pack_into('<QQI', d, 0x148, 0x200, 0x1000, 0x100)  # control, code, code size
    struct.pack_into('<II', d, 0x200 + 0x6FC, 0x20, 0x40)     # constants: size, offset
    code = bytes((i * 7 + seed) & 0xFF for i in range(0x60))
    d[0x1000:0x1060] = code
    return bytes(d), code


def glslc(code):
    """One GLSLC output: a header, then one GPU-code section: control (0x880 bytes, constants size 0x20 at 0x6FC and
    offset 0x40 at 0x700), then the 0x60-byte code."""
    ctl = bytearray(0x880)
    struct.pack_into('<II', ctl, 0x6FC, 0x20, 0x40)
    sec = bytes(ctl) + code
    head = bytearray(0x120)
    head[0:4] = b'\x91\x68\x86\x19'
    struct.pack_into('<I', head, 0x44, len(head) + len(sec))  # total length
    struct.pack_into('<I', head, 0x4C, 1)                      # section count
    struct.pack_into('<III', head, 0x90, len(sec), 0x120, 0)  # section: size, data offset, GPU code
    struct.pack_into('<IIII', head, 0xC0, 0, len(ctl), len(code), len(ctl))  # control and code offsets, lengths
    return bytes(head) + sec


def u8_archive(files):
    """A Wii U U8 archive (big-endian): header, root node, one node per file, names, then the files (32-byte aligned,
    their data offsets relative to the data's start)."""
    strings, names = bytearray(b'\0'), []
    for name, _ in files:
        names.append(len(strings))
        strings += name.encode() + b'\0'
    count = len(files) + 1
    table = count * 12 + len(strings)
    data_off = (0x20 + table + 0x1F) & ~0x1F
    nodes = struct.pack('>III', 0x01000000, 0, count)  # root: a directory; its next index is the node count
    data = bytearray()
    for (_, body), name in zip(files, names):
        data += b'\0' * (-len(data) % 0x20)
        nodes += struct.pack('>III', name, len(data), len(body))
        data += body
    head = struct.pack('>4I', 0x55AA382D, 0x20, table, data_off) + bytes(16)
    arc = head + nodes + strings
    return arc + b'\0' * (data_off - len(arc)) + bytes(data)


def romfs_image(files):
    """A RomFS image: header, one directory, the files' table, then their data (16-byte aligned)."""
    data, meta = bytearray(), bytearray()
    for name, body in files:
        data += b'\0' * (-len(data) % 16)
        n = name.encode()
        meta += struct.pack('<IIQQII', 0, 0xFFFFFFFF, len(data), len(body), 0xFFFFFFFF, len(n)) + n + b'\0' * (-len(n) % 4)
        data += body
    dmeta = struct.pack('<IIIIII', 0, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0xFFFFFFFF, 0)
    fmeta_off = 0x50 + len(dmeta)
    data_off = (fmeta_off + len(meta) + 0xF) & ~0xF
    head = struct.pack('<10Q', 0x50, 0x50, 0, 0x50, len(dmeta), fmeta_off, 0, fmeta_off, len(meta), data_off)
    img = head + dmeta + meta
    return img + b'\0' * (data_off - len(img)) + data


def fnv(b):
    h = 0xcbf29ce484222325
    for x in b:
        h = ((h ^ x) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    a, code_a = bnsh(1)
    b, code_b = bnsh(2)
    code_c = bytes((i * 11 + 5) & 0xFF for i in range(0x60))
    arc = u8_archive([('Shaders/cache/c.bin', bytes(0x10) + glslc(code_c)), ('Shaders/Dummy.frag', b'not a shader' * 50)])
    half = len(arc) // 2
    packed = gzip.compress(arc[:half]) + gzip.compress(arc[half:])  # two gzip members, one archive
    (directory / 'romfs.bin').write_bytes(romfs_image([
        ('Readme.txt', b'not a shader' * 50),
        ('Object.bfsha', a),
        ('Other.bin', b'BNSH\0\0\0\0' + bytes(0x100)),  # (not a shader file by name: skipped)
        ('Layout.arc', b'\0' * 0x38 + b),
        ('Shaders.arc.gz', packed),
        ('Damaged.arc.gz', packed[:-4]),  # (a truncated stream: nothing of it is indexed)
    ]))
    (directory / 'code_a').write_bytes(code_a)
    (directory / 'code_b').write_bytes(code_b)
    (directory / 'code_c').write_bytes(code_c)
    exe = directory / ('shader-index-test.exe' if os.name == 'nt' else 'shader-index-test')
    src = directory / 'index.c'
    src.write_text('#include <dirent.h>\n#include <inttypes.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stdio.h>\n'
                   '#include <stdlib.h>\n#include <string.h>\n#include <sys/stat.h>\n#include <zlib.h>\n'
                   '#include "nvn/shader_prep.h"\n' + hash_fn + index + '#include "test_shader_index.c"\n')
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) +
                   ['-std=gnu11', '-Iruntime', '-Itools', str(src), '-o', str(exe), '-lz'], cwd=ROOT, check=True)
    subprocess.run([str(exe), (directory / 'romfs').as_posix(), '%016x' % fnv(code_a), '%016x' % fnv(code_b),
                    (directory / 'code_a').as_posix(), (directory / 'code_b').as_posix(),
                    '%016x' % fnv(code_c), (directory / 'code_c').as_posix()], check=True)
