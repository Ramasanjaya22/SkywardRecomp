"""Skyward Sword HD's shader archive (Shaders/Shaders.arc.gz, runtime/nvn/shader_prep.c ss_arc_each): the stage codes
of its program files are found where the game hands them to NVN. A synthetic U8 archive, no game files.
Run: python3 tools/test_ss_shader_arc.py (CC: the compiler, default clang)."""
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
source = (ROOT / 'runtime/nvn/shader_prep.c').read_text()
start = source.index('// ------------------------------------------------------------------ SS shader archive')
end = source.index('// (end of the archive parser', start)


def program(codes):  # a program file: header, stage table at 0x90, one block per stage (code at +0x880)
    f = bytearray(0x90 + 0x90 * len(codes))
    struct.pack_into('<I', f, 0, 0x19866891)
    struct.pack_into('<I', f, 0x4c, len(codes))
    for i, c in enumerate(codes):
        boff = len(f)
        f += bytes(0x880) + c
        e = 0x90 + 0x90 * i
        struct.pack_into('<II', f, e, 0x880 + len(c), boff)
        struct.pack_into('<II', f, e + 0x38, len(c), 0x880)
    return bytes(f)


def u8(files):  # U8: header, node table (root + files), data
    nodes = 1 + len(files)
    root = 0x20
    data = (root + 12 * nodes + 0x1f) & ~0x1f
    a = bytearray(data)
    a[0:4] = b'\x55\xaa\x38\x2d'
    struct.pack_into('>I', a, 4, root)
    struct.pack_into('>BxxxII', a, root, 1, 0, nodes)
    offs = []
    for i, f in enumerate(files):
        offs.append(len(a))
        struct.pack_into('>BxxxII', a, root + 12 * (i + 1), 0, len(a), len(f))
        a += f + bytes(-len(f) & 0x1f)
    return bytes(a), offs


hdr = b'\x78\x56\x34\x12' + bytes(0x2c)
vs, fs, cs = hdr + b'V' * 0xd0, hdr + b'F' * 0x1d0, hdr + b'C' * 0x50
other = struct.pack('<I', 2) + bytes(0x100)  # (the archive's other files: not programs)
arc, offs = u8([program([vs, fs]), other, program([cs])])
want = sorted([(offs[0] + 0x90 * 3 + 0x880, len(vs)), (offs[0] + 0x90 * 3 + 0x880 * 2 + len(vs), len(fs)),
               (offs[2] + 0x90 * 2 + 0x880, len(cs))])

test_c = r'''
static void add(void *ctx, uint32_t off, uint32_t size) { printf("%u %u\n", off, size); }
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb");
    static uint8_t a[1 << 20];
    size_t n = fread(a, 1, sizeof a, f);
    printf("n %d\n", ss_arc_each(a, n, add, NULL));
    printf("cut %d\n", ss_arc_each(a, n - 0x40, add, NULL) <= 2);  // (a cut archive: what is inside it, no reads past)
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    d = Path(d)
    (d / 'arc.bin').write_bytes(arc)
    src = d / 't.c'
    src.write_text('#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n' + source[start:end] + test_c)
    exe = d / ('t.exe' if os.name == 'nt' else 't')
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) + ['-std=gnu11', '-fsanitize=address' if os.name != 'nt'
                   else '-O1', str(src), '-o', str(exe)], check=True)
    out = subprocess.run([str(exe), str(d / 'arc.bin')], check=True, capture_output=True, text=True).stdout.split('\n')
    full = out[:out.index('n 3') + 1]
    got = sorted(tuple(map(int, l.split())) for l in full[:-1])
    assert full[-1] == 'n 3', out
    assert got == want, (got, want)
    assert 'cut 1' in out, out
    for off, size in got:
        assert arc[off:off + 4] == b'\x78\x56\x34\x12', off  # (the NVN code header starts each)
print('PASS: SS shader archive: 3 stage codes in 2 programs, other files skipped, a cut archive read safely')
